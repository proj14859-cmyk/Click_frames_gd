#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>
#include <fstream>

using namespace geode::prelude;

struct Ev { int frame; bool down; int button; bool p1; };

// shortcut: global state, one level at a time; move into Fields if you ever need more.
static std::vector<Ev> g_rec;
static size_t g_next = 0;
static int g_lastPress = -1;
static bool g_injecting = false;
static std::string g_mode = "off";

static constexpr auto LABEL_ID = "click-label"_spr;

$execute {
    g_mode = Mod::get()->getSettingValue<std::string>("mode");
    listenForSettingChanges<std::string>("mode", [](std::string v) { g_mode = v; });
}

static std::filesystem::path recPath(GJGameLevel* l) {
    return Mod::get()->getSaveDir() / fmt::format("rec-{}.csv", (int)l->m_levelID.value());
}

static void saveRec(GJGameLevel* l) {
    std::ofstream f(recPath(l));
    for (auto& e : g_rec) f << e.frame << ',' << e.down << ',' << e.button << ',' << e.p1 << '\n';
}

static void loadRec(GJGameLevel* l) {
    g_rec.clear();
    std::ifstream f(recPath(l));
    Ev e; char c; int d, p;
    while (f >> e.frame >> c >> d >> c >> e.button >> c >> p) { e.down = d; e.p1 = p; g_rec.push_back(e); }
}

// gap in frames since the previous click: shown on screen and played as a sound (1-20, else "wide")
static void showGap(PlayLayer* pl, int gap) {
    auto label = static_cast<CCLabelBMFont*>(pl->getChildByID(LABEL_ID));
    if (!label) {
        label = CCLabelBMFont::create("", "bigFont.fnt");
        label->setID(LABEL_ID);
        label->setScale(0.8f);
        auto win = CCDirector::get()->getWinSize();
        label->setPosition({win.width / 2, win.height - 30});
        pl->addChild(label, 100);
    }
    label->setString(fmt::format("Gap: {} ticks - ventana sin medir", gap).c_str());
    if (gap < 1 || !Mod::get()->getSettingValue<bool>("sounds")) return;
    auto snd = gap > 9 ? std::string("frame-wide.wav") : fmt::format("frame-{}.wav", gap);
    FMODAudioEngine::get()->playEffect(fmt::format("{}/{}", Mod::get()->getID(), snd));
}

class $modify(CFBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || g_mode == "off")
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        if (g_mode == "replay" && !g_injecting) return; // replay ignores real input

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        int frame = m_gameState.m_currentProgress; // verify member name against your bindings
        if (g_mode == "record") {
            g_rec.push_back({frame, down, button, isPlayer1});
        } else if (down && button == 1) {
            showGap(pl, g_lastPress < 0 ? 0 : frame - g_lastPress);
            g_lastPress = frame;
        }
    }

    void processCommands(float dt) {
        auto pl = PlayLayer::get();
        if (g_mode == "replay" && pl && static_cast<GJBaseGameLayer*>(pl) == this) {
            int f = m_gameState.m_currentProgress - Mod::get()->getSettingValue<int64_t>("replay-offset");
            g_injecting = true;
            while (g_next < g_rec.size() && g_rec[g_next].frame <= f) {
                auto& e = g_rec[g_next++];
                this->handleButton(e.down, e.button, e.p1);
            }
            g_injecting = false;
        }
        GJBaseGameLayer::processCommands(dt);
    }
};

class $modify(CFPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        g_mode = "off";
        g_rec.clear();
        g_next = 0;
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        g_lastPress = -1;
        g_next = 0;
        if (g_mode == "record") {
            int tick = m_gameState.m_currentProgress;
            g_rec.erase(std::remove_if(g_rec.begin(), g_rec.end(), [tick](auto e) {
                return e.frame >= tick;
            }), g_rec.end());
        }
        if (g_mode == "replay") loadRec(m_level);
        if (auto label = static_cast<CCLabelBMFont*>(this->getChildByID(LABEL_ID))) label->setString("");
    }

    void levelComplete() {
        bool recorded = g_mode == "record";
        if (recorded) { saveRec(m_level); g_mode = "off"; }
        PlayLayer::levelComplete();
        if (recorded) FLAlertLayer::create("Click Frames", "Run guardada. Abre pausa > Click Frames > Play para probarla. Las ventanas validas todavia no se han medido.", "OK")->show();
    }
};

class $modify(CFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = CCMenu::create();
        menu->setPosition({CCDirector::get()->getWinSize().width / 2, 35});
        auto item = CCMenuItemSpriteExtra::create(ButtonSprite::create("Click Frames", 0, 0, .6f), this, menu_selector(CFPauseLayer::openMenu));
        menu->addChild(item); addChild(menu, 100);
    }
    void openMenu(CCObject*) {
        if (auto panel = getChildByID("cf-panel")) { panel->removeFromParent(); return; }
        auto size = CCDirector::get()->getWinSize();
        auto panel = CCLayerColor::create({0, 0, 0, 245}, size.width, size.height);
        panel->setID("cf-panel"); addChild(panel, 200);
        auto menu = CCMenu::create(); menu->setPosition({size.width / 2, size.height / 2}); panel->addChild(menu);
        struct Action { char const* title; SEL_MenuHandler callback; };
        Action actions[] = {
            {"Record", menu_selector(CFPauseLayer::record)},
            {"Play / Replay", menu_selector(CFPauseLayer::play)},
            {"Settings", menu_selector(CFPauseLayer::settings)},
            {"Stop", menu_selector(CFPauseLayer::stop)},
            {"Close", menu_selector(CFPauseLayer::openMenu)}
        };
        for (int i = 0; i < 5; ++i) {
            auto item = CCMenuItemSpriteExtra::create(ButtonSprite::create(actions[i].title, 0, 0, .6f), this, actions[i].callback);
            item->setPosition({0, 80.f - i * 40.f}); menu->addChild(item);
        }
    }
    void record(CCObject*) {
        auto pl = PlayLayer::get(); g_mode = "off";
        pl->togglePracticeMode(false); pl->resetLevel(); pl->togglePracticeMode(true);
        g_rec.clear(); g_next = 0; g_lastPress = -1; g_mode = "record";
        onResume(nullptr);
    }
    void play(CCObject*) {
        auto pl = PlayLayer::get();
        loadRec(pl->m_level);
        if (g_rec.empty()) { FLAlertLayer::create("Click Frames", "No hay una run guardada para este nivel.", "OK")->show(); return; }
        g_mode = "off"; pl->togglePracticeMode(false);
        g_mode = "replay"; pl->resetLevel(); onResume(nullptr);
    }
    void stop(CCObject*) { g_mode = "off"; openMenu(nullptr); }
    void settings(CCObject*) { openSettingsPopup(Mod::get()); }
};
