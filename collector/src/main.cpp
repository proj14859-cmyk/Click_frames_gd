#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "CollectorData.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace {
struct CollectorSession {
    PlayLayer* owner = nullptr;
    cfc::Attempt attempt;
    bool active = false, finalizing = false;
    int lastTick = -1;
    uint64_t sequence = 0, sampleCounter = 0;
    std::array<bool, 6> held{};
} s;

int64_t unixMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::filesystem::path saveRoot() {
    auto converted=Mod::get()->getSaveDir().toStdPath();
    return converted ? converted.unwrap() : std::filesystem::path{};
}
bool normalMode(PlayLayer* layer) {
    return layer && Mod::get()->getSettingValue<bool>("enabled") && !layer->m_isPracticeMode &&
        !layer->m_isTestMode && layer->m_level;
}
int mode(PlayerObject* p) {
    return !p ? -1 : p->m_isShip ? 1 : p->m_isBall ? 2 : p->m_isBird ? 3 : p->m_isDart ? 4 :
        p->m_isRobot ? 5 : p->m_isSpider ? 6 : p->m_isSwing ? 7 : 0;
}
int slot(int button,bool p1){return (button-1)*2+int(!p1);}

bool writeFile(std::filesystem::path const& path,std::string const& bytes) {
    std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
    auto temporary=path;temporary+=".tmp";
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!out)return false;}
    std::filesystem::rename(temporary,path,ec);return !ec;
}
bool appendIndex(std::filesystem::path const& root,cfc::Attempt const& a) {
    std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return false;
    auto index=root/"index.tsv";bool header=!std::filesystem::exists(index,ec);
    std::ofstream out(index,std::ios::app);
    if(header)out<<"schema\tattempt_id\tlevel_id\tlevel_hash\tresult\tpercent\tstart_ms\tend_ms\tinputs\tsamples\n";
    out<<cfc::schema<<'\t'<<a.id<<'\t'<<a.levelID<<'\t'<<a.levelHash<<'\t'<<a.result<<'\t'<<a.endPercent<<'\t'
       <<a.startedUnixMs<<'\t'<<a.endedUnixMs<<'\t'<<a.inputs.size()<<'\t'<<a.frames.size()<<'\n';
    return bool(out);
}
bool writeCategory(std::string const& category,cfc::Attempt const& a) {
    auto categoryRoot=saveRoot()/category;
    auto levelFolder=cfc::safeFolder(a.levelName)+"-"+std::to_string(a.levelID);
    auto directory=categoryRoot/levelFolder/a.id;
    return writeFile(directory/"metadata.json",cfc::metadata(a)) &&
        writeFile(directory/"inputs.tsv",cfc::inputsTsv(a)) &&
        writeFile(directory/"trajectory.tsv",cfc::trajectoryTsv(a)) && appendIndex(categoryRoot,a);
}
void startAttempt(PlayLayer* layer) {
    if(!normalMode(layer) || s.finalizing)return;
    s.owner=layer;s.attempt={};s.active=true;s.lastTick=-1;s.sampleCounter=0;s.held.fill(false);
    auto level=layer->m_level;auto stamp=unixMs();
    s.attempt.startedUnixMs=stamp;s.attempt.levelID=level->m_levelID.value();s.attempt.levelName=level->m_levelName;
    s.attempt.levelHash=std::to_string(cfc::fnv1a(level->m_levelString));
    s.attempt.stars=level->m_stars.value();s.attempt.demon=level->m_demon.value();s.attempt.demonDifficulty=level->m_demonDifficulty;
    s.attempt.length=level->m_levelLength;s.attempt.gameVersion=level->m_gameVersion;s.attempt.platformer=layer->m_isPlatformer;
    s.attempt.twoPlayer=level->m_twoPlayerMode;s.attempt.attemptNumber=layer->m_attempts;
    s.attempt.id=std::to_string(stamp)+"-"+std::to_string(++s.sequence);
}
void finishAttempt(std::string result,float percent,int deathID=0,int deathType=0,float deathX=0,float deathY=0) {
    if(!s.active || s.finalizing)return;
    s.finalizing=true;s.active=false;
    s.attempt.endedUnixMs=unixMs();s.attempt.result=std::move(result);s.attempt.endPercent=std::clamp(percent,0.f,100.f);
    s.attempt.completed=s.attempt.result=="completed";s.attempt.endTick=s.owner?int(s.owner->m_gameState.m_currentProgress):s.lastTick;
    s.attempt.deathObjectID=deathID;s.attempt.deathObjectType=deathType;s.attempt.deathX=deathX;s.attempt.deathY=deathY;
    if(!writeCategory("normal-attempts",s.attempt))log::error("[Collector] Could not save normal attempt {}",s.attempt.id);
    if(s.attempt.completed && !writeCategory("completed-runs",s.attempt))log::error("[Collector] Could not save completed run {}",s.attempt.id);
    s.finalizing=false;
}
void sample(PlayLayer* layer) {
    if(!s.active || s.owner!=layer || !normalMode(layer) || !layer->m_player1 || !layer->m_player2)return;
    int tick=int(layer->m_gameState.m_currentProgress);if(tick==s.lastTick)return;s.lastTick=tick;
    auto stride=std::max<int64_t>(1,Mod::get()->getSettingValue<int64_t>("sample-stride"));
    if(s.sampleCounter++%uint64_t(stride))return;
    auto limit=std::max<int64_t>(2400,Mod::get()->getSettingValue<int64_t>("max-samples"));
    if(s.attempt.frames.size()>=size_t(limit)){s.attempt.samplesTruncated=true;return;}
    auto p1=layer->m_player1,p2=layer->m_player2;cfc::Frame f;
    f.tick=tick;f.percent=layer->getCurrentPercent();f.x1=p1->getPositionX();f.y1=p1->getPositionY();f.vy1=p1->m_yVelocity;f.speed1=p1->m_playerSpeed;
    f.mode1=mode(p1);f.gravity1=p1->m_isUpsideDown;f.mini1=p1->m_isMini;f.grounded1=p1->m_isOnGround;
    f.x2=p2->getPositionX();f.y2=p2->getPositionY();f.vy2=p2->m_yVelocity;f.speed2=p2->m_playerSpeed;
    f.mode2=mode(p2);f.gravity2=p2->m_isUpsideDown;f.mini2=p2->m_isMini;f.grounded2=p2->m_isOnGround;f.dual=layer->m_gameState.m_isDualMode;
    s.attempt.frames.push_back(f);
}
void input(GJBaseGameLayer* layer,bool down,int button,bool p1) {
    if(!s.active || !s.owner || static_cast<GJBaseGameLayer*>(s.owner)!=layer || !normalMode(s.owner) || button<1 || button>3 ||
        s.owner->m_isPaused || s.owner->m_inResetDelay || s.owner->m_hasCompletedLevel)return;
    if(!s.owner->m_level->m_twoPlayerMode || !s.owner->m_gameState.m_isDualMode)p1=true;
    auto index=slot(button,p1);if(s.held[index]==down)return;s.held[index]=down;
    auto player=p1?s.owner->m_player1:s.owner->m_player2;cfc::InputEvent e;
    e.tick=int(s.owner->m_gameState.m_currentProgress);e.down=down;e.button=button;e.p1=p1;e.mode=mode(player);
    if(player){e.x=player->getPositionX();e.y=player->getPositionY();}s.attempt.inputs.push_back(e);
}
}

class $modify(CFCInput,GJBaseGameLayer) {
    void handleButton(bool down,int button,bool p1) {
        input(this,down,button,p1);GJBaseGameLayer::handleButton(down,button,p1);
    }
};

class $modify(CFCPlay,PlayLayer) {
    bool init(GJGameLevel* level,bool dontCreateObjects) {
        if(!PlayLayer::init(level,dontCreateObjects))return false;
        startAttempt(this);return true;
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if(s.owner==this && s.active && !normalMode(this))finishAttempt("mode-change",getCurrentPercent());
        else if(s.owner==this && !s.active && normalMode(this) && !m_hasCompletedLevel && !m_player1->m_isDead)startAttempt(this);
        sample(this);
    }
    void resetLevel() {
        if(s.owner==this && s.active)finishAttempt("restart",getCurrentPercent());
        PlayLayer::resetLevel();
        if(normalMode(this))startAttempt(this);
    }
    void destroyPlayer(PlayerObject* player,GameObject* object) {
        bool tracking=s.owner==this&&s.active&&normalMode(this);float percent=tracking?getCurrentPercent():0;
        int id=object?object->m_uniqueID:0,type=object?object->m_objectID:0;float x=object?object->getPositionX():0,y=object?object->getPositionY():0;
        PlayLayer::destroyPlayer(player,object);
        if(tracking&&player&&player->m_isDead)finishAttempt("death",percent,id,type,x,y);
    }
    void levelComplete() {
        if(s.owner==this&&s.active&&normalMode(this))finishAttempt("completed",100.f);
        PlayLayer::levelComplete();
    }
    void onExit() {
        if(s.owner==this&&s.active)finishAttempt("quit",getCurrentPercent());
        if(s.owner==this)s.owner=nullptr;
        PlayLayer::onExit();
    }
};
