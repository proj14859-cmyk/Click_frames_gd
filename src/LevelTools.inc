#include <deque>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCMotionStreak.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCParticleBatchNode.hpp>
#include <Geode/modify/HardStreak.hpp>

namespace {
bool tool(char const* key){return Mod::get()->getSettingValue<bool>(key);}
struct ToolTab{char const* name;char const* note;std::vector<std::string> keys;};
std::vector<ToolTab> toolTabs(){
    std::vector<ToolTab> tabs{
        {"Visual","Cosmetic. Never affects statistics or analysis.",{"tool-no-particles","tool-no-trail","tool-no-death","tool-no-wave","tool-solid-wave","tool-width"}},
        {"Guides","Rough estimates. Account stats stay protected.",{"tool-boxes","tool-path","tool-path-boxes","tool-force"}},
        {"Assist","Noclip blocks Record/Analyze; collector ignores both.",{"tool-noclip","tool-speed","tool-safe"}},
        {"HUD","Elapsed / total level time and FPS.",{"tool-fps","tool-ms","tool-decimals","tool-time","tool-time-total","tool-time-decimals","tool-dot"}},
        {"Position","Where the HUD sits and how it looks.",{"tool-x","tool-y","tool-scale","tool-opacity","tool-refresh"}},
    };
    ToolTab other{"Other","Other tool settings.",{}};
    for(auto key:Mod::get()->getSettingKeys()){
        if(!key.starts_with("tool-"))continue;
        bool known=std::any_of(tabs.begin(),tabs.end(),[&](auto const& tab){return std::find(tab.keys.begin(),tab.keys.end(),key)!=tab.keys.end();});
        if(!known)other.keys.push_back(key);
    }
    if(!other.keys.empty())tabs.push_back(std::move(other));
    return tabs;
}
void resetToolSetting(std::string const& key){
    struct FloatDefault{char const* key;double value;};
    static constexpr FloatDefault floats[]={
        {"tool-width",1.0},{"tool-speed",1.0},{"tool-x",50.0},{"tool-y",93.0},
        {"tool-scale",1.0},{"tool-opacity",100.0},{"tool-refresh",250.0}
    };
    for(auto const& item:floats){
        if(key==item.key){Mod::get()->setSettingValue<double>(key,item.value);return;}
    }
    Mod::get()->setSettingValue<bool>(key,key=="tool-time-decimals" || key=="tool-time-total");
}
bool isToolRowEnabled(std::string const& key){
    if(key=="tool-path-boxes")return tool("tool-path");
    if(key=="tool-ms" || key=="tool-decimals")return tool("tool-fps");
    if(key=="tool-time-total" || key=="tool-time-decimals")return tool("tool-time");
    return true;
}
void dimToolNode(CCNode* node,bool enabled){
    if(!node)return;
    if(auto rgba=typeinfo_cast<CCRGBAProtocol*>(node))rgba->setOpacity(enabled?255:110);
    if(auto children=node->getChildren()){
        for(unsigned i=0;i<children->count();++i){
            if(auto child=typeinfo_cast<CCNode*>(children->objectAtIndex(i)))dimToolNode(child,enabled);
        }
    }
}
class ToolsPopup : public ClassicPopup {
    std::vector<ToolTab> tabs;
    std::vector<SettingNodeV3*> rows;
    CCNode* list=nullptr;
    CCMenu* dynamic=nullptr;
    CCLabelBMFont* chip=nullptr;
    CCLabelBMFont* note=nullptr;
    int tab=0;
    void refreshDependencies(){
        if(tab<0 || size_t(tab)>=tabs.size())return;
        auto const& keys=tabs[tab].keys;
        for(size_t i=0;i<rows.size() && i<keys.size();++i)dimToolNode(rows[i],isToolRowEnabled(keys[i]));
    }
    void commit(){
        bool changed=false;
        for(auto row:rows)if(row->hasUncommittedChanges()){row->commit();changed=true;}
        if(changed){syncToolProtection();refreshDependencies();}
    }
    // Same state the HUD dot shows: red for assists that change play, amber for guides that only protect stats.
    void refreshChip(){
        auto mod=Mod::get();std::string text;ccColor3B color{90,255,130};
        if(mod->getSettingValue<bool>("tool-noclip"))text+=" Noclip";
        auto speed=mod->getSettingValue<double>("tool-speed");
        if(speed!=1.)text+=fmt::format(" Speed {:.2f}x",speed);
        if(!text.empty()){text="ASSISTED:"+text;color={255,80,80};}
        else if(mod->getSettingValue<bool>("tool-boxes") || mod->getSettingValue<bool>("tool-path") ||
            mod->getSettingValue<bool>("tool-path-boxes") || mod->getSettingValue<bool>("tool-safe")){
            text="CLEAN (account stats protected)";color={255,210,80};
        }else text="CLEAN";
        chip->setString(text.c_str());chip->setColor(color);
    }
    // Rows apply as soon as they change, so the HUD and guides update while this menu is open.
    void poll(float){commit();refreshChip();}
    void showTab(){
        commit();
        try{Mod::get()->setSavedValue<int>("tools-tab",tab);}catch(...){}
        rows.clear();list->removeAllChildren();dynamic->removeAllChildren();
        auto const& current=tabs[tab];
        note->setString(current.note);
        float rowStep=tab==3?26.f:33.f;
        float startY=tab==3?212.f:201.f;
        for(size_t i=0;i<current.keys.size();++i){
            auto setting=Mod::get()->getSetting(current.keys[i]);
            auto row=setting?setting->createNode(320):nullptr;
            if(!row)continue;
            if(tab==3)row->setScale(.88f);
            row->setAnchorPoint({0,0});row->setPosition({100,startY-rowStep*i});
            list->addChild(row);rows.push_back(row);
        }
        for(int i=0;i<int(tabs.size());++i){
            auto sprite=ButtonSprite::create(tabs[i].name);sprite->setScale(i==tab?.5f:.4f);
            auto item=CCMenuItemExt::createSpriteExtra(sprite,[this,i](auto){tab=i;showTab();});
            item->setPosition({52,232.f-34*i});dynamic->addChild(item);
            if(i==tab){auto mark=CCLabelBMFont::create(">","bigFont.fnt");mark->setScale(.4f);mark->setPosition({14,232.f-34*i});dynamic->addChild(mark);}
        }
        if(tab==3){ // HUD tab: quick presets
            auto fpsSprite=ButtonSprite::create("Solo FPS");fpsSprite->setScale(.45f);
            auto fpsItem=CCMenuItemExt::createSpriteExtra(fpsSprite,[this](auto){
                auto mod=Mod::get();
                mod->setSettingValue<bool>("tool-fps",true);
                mod->setSettingValue<bool>("tool-ms",false);
                mod->setSettingValue<bool>("tool-decimals",false);
                mod->setSettingValue<bool>("tool-time",false);
                mod->setSettingValue<bool>("tool-time-total",false);
                mod->setSettingValue<bool>("tool-dot",false);
                showTab();
            });
            fpsItem->setPosition({190,42});dynamic->addChild(fpsItem);
            auto fullSprite=ButtonSprite::create("Completo");fullSprite->setScale(.45f);
            auto fullItem=CCMenuItemExt::createSpriteExtra(fullSprite,[this](auto){
                auto mod=Mod::get();
                for(char const* k:{"tool-fps","tool-ms","tool-decimals","tool-time","tool-time-total","tool-time-decimals","tool-dot"})
                    mod->setSettingValue<bool>(k,true);
                showTab();
            });
            fullItem->setPosition({330,42});dynamic->addChild(fullItem);
        }
        if(tab==4){ // Position tab: one-tap HUD placements
            struct Spot{char const* name;double x,y;float px;};
            for(auto spot:{Spot{"Centre",50.,93.,190.f},Spot{"Top left",2.,96.,330.f}}){
                auto sprite=ButtonSprite::create(spot.name);sprite->setScale(.5f);
                auto item=CCMenuItemExt::createSpriteExtra(sprite,[this,spot](auto){
                    Mod::get()->setSettingValue<double>("tool-x",spot.x);Mod::get()->setSettingValue<double>("tool-y",spot.y);showTab();});
                item->setPosition({spot.px,52});dynamic->addChild(item);
            }
        }
        refreshDependencies();
        refreshChip();
    }
    void onClose(CCObject* sender) override {commit();Popup::onClose(sender);}
    bool init(){
        if(!ClassicPopup::init(440,300))return false;
        setTitle("Level tools");
        tabs=toolTabs();
        tab=std::clamp(int(Mod::get()->getSavedValue<int>("tools-tab",0)),0,int(tabs.size())-1);
        list=CCNode::create();m_mainLayer->addChild(list);
        chip=CCLabelBMFont::create("","bigFont.fnt");chip->setScale(.4f);chip->setPosition({220,262});m_mainLayer->addChild(chip);
        note=CCLabelBMFont::create("","chatFont.fnt");note->setScale(.5f);note->setPosition({260,248});m_mainLayer->addChild(note);
        dynamic=CCMenu::create();dynamic->setPosition({0,0});m_mainLayer->addChild(dynamic,2);
        auto footer=CCMenu::create();footer->setPosition({0,0});m_mainLayer->addChild(footer,2);
        auto resetSprite=ButtonSprite::create("Reset tab");resetSprite->setScale(.5f);
        auto reset=CCMenuItemExt::createSpriteExtra(resetSprite,[this](auto){
            for(auto const& key:tabs[tab].keys)resetToolSetting(key);
            syncToolProtection();showTab();});
        reset->setPosition({150,18});footer->addChild(reset);
        auto closeSprite=ButtonSprite::create("Close");closeSprite->setScale(.5f);
        auto close=CCMenuItemExt::createSpriteExtra(closeSprite,[this](auto){onClose(nullptr);});
        close->setPosition({290,18});footer->addChild(close);
        showTab();
        schedule(schedule_selector(ToolsPopup::poll),.25f);
        return true;
    }
public:
    static void open(){auto p=new ToolsPopup;if(p->init()){p->autorelease();p->show();}else delete p;}
};
void showLevelTools(){ToolsPopup::open();}
void outline(CCDrawNode* draw,CCRect rect,ccColor4F color){
    CCPoint points[]={{rect.getMinX(),rect.getMinY()},{rect.getMaxX(),rect.getMinY()},
        {rect.getMaxX(),rect.getMaxY()},{rect.getMinX(),rect.getMaxY()}};
    draw->drawPolygon(points,4,{0,0,0,0},.7f,color);
}
void drawGuides(){
    auto layer=s.owner;
    if(!layer->m_objectLayer || !layer->m_player1)return;
    auto node=static_cast<CCDrawNode*>(layer->m_objectLayer->getChildByID("cf-tool-guides"));
    bool boxes=tool("tool-boxes"),path=tool("tool-path"),pathBoxes=tool("tool-path-boxes");
    bool force=tool("tool-force");
    if(s.state==State::Playback && Mod::get()->getSettingValue<bool>("trajectory-preview") && !s.visual.enabled)
        path=pathBoxes=false;
    if(!(boxes || path || pathBoxes || force)){if(node)node->removeFromParent();return;}
    if(!node){node=CCDrawNode::create();node->setID("cf-tool-guides");layer->m_objectLayer->addChild(node,1000);}
    node->clear();
    constexpr ccColor4F blue{.2f,.6f,1,1},red{1,.16f,.18f,1};
    std::vector<CCRect> hazards;
    struct GuideCache {
        PlayLayer* layer=nullptr;
        unsigned attempt=0;
        unsigned objectCount=0;
        int lastTick=-1000;
        CCPoint p1Pos{};
        std::vector<GameObject*> nearby;
    };
    static GuideCache cache;
    if((boxes || path || pathBoxes) && layer->m_objects){
        int now=tick();
        unsigned count=layer->m_objects->count();
        auto p1Pos=layer->m_player1->getPosition();
        bool refresh=cache.layer!=layer || cache.attempt!=s.toolAttempt || cache.objectCount!=count ||
            std::abs(now-cache.lastTick)>=6 || p1Pos.getDistance(cache.p1Pos)>120.f;
        if(refresh){
            cache.layer=layer;cache.attempt=s.toolAttempt;cache.objectCount=count;
            cache.lastTick=now;cache.p1Pos=p1Pos;cache.nearby.clear();
            auto nearWindow=[&](PlayerObject* p,GameObject* obj){
                return p && std::abs(p->getPositionX()-obj->getPositionX())<850.f &&
                    std::abs(p->getPositionY()-obj->getPositionY())<560.f;
            };
            for(unsigned i=0;i<count;++i){
                auto obj=static_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
                if(!obj)continue;
                bool hazard=obj->m_objectType==GameObjectType::Hazard || obj->m_objectType==GameObjectType::AnimatedHazard;
                if(!hazard && obj->m_objectType!=GameObjectType::Solid && obj->m_objectType!=GameObjectType::Slope &&
                    obj->m_objectType!=GameObjectType::Breakable)continue;
                if(!nearWindow(layer->m_player1,obj) && !(layer->m_gameState.m_isDualMode && nearWindow(layer->m_player2,obj)))continue;
                cache.nearby.push_back(obj);
            }
        }
        auto closeToPlayer=[&](PlayerObject* p,GameObject* obj){
            return p && std::abs(p->getPositionX()-obj->getPositionX())<700 &&
                std::abs(p->getPositionY()-obj->getPositionY())<450;
        };
        for(auto obj:cache.nearby){
            if(!obj || obj->m_isDisabled || obj->m_isDisabled2)continue;
            if(!closeToPlayer(layer->m_player1,obj) && !(layer->m_gameState.m_isDualMode && closeToPlayer(layer->m_player2,obj)))continue;
            bool hazard=obj->m_objectType==GameObjectType::Hazard || obj->m_objectType==GameObjectType::AnimatedHazard;
            auto rect=obj->getObjectRect();
            if(hazard)hazards.push_back(rect);
            if(boxes){
                if(hazard && obj->m_objectRadius>0)node->drawCircle(obj->getPosition(),obj->m_objectRadius,{0,0,0,0},1,red,32);
                else if(obj->m_orientedBox){
                    auto corners=obj->m_orientedBox->m_corners;
                    node->drawPolygon(corners.data(),4,{0,0,0,0},.7f,hazard?red:blue);
                }else outline(node,rect,hazard?red:blue);
            }
        }
    }
    struct Sample{int tick;CCPoint pos;};
    struct History{PlayLayer* layer=nullptr;unsigned attempt=0;std::deque<Sample> samples;};
    static std::array<History,2> recent;
    for(int id=0;id<(layer->m_gameState.m_isDualMode?2:1);++id){
        auto p=id?layer->m_player2:layer->m_player1;if(!p)continue;
        auto pos=p->getPosition();auto rect=p->getObjectRect();auto inner=p->getObjectRect(.3f,.3f);
        if(boxes){outline(node,rect,blue);outline(node,inner,red);}
        auto& h=recent[id];int now=tick();auto& samples=h.samples;
        if(h.layer!=layer || h.attempt!=s.toolAttempt || (!samples.empty() &&
            (now<samples.back().tick || now-samples.back().tick>12 || pos.getDistance(samples.back().pos)>100)))samples.clear();
        h.layer=layer;h.attempt=s.toolAttempt;
        if(samples.empty() || samples.back().tick!=now)samples.push_back({now,pos});
        while(samples.size()>2 && samples.front().tick<now-32)samples.pop_front();
        if(samples.size()<2)continue;
        if(force)for(size_t j=1;j<samples.size();++j)node->drawSegment(samples[j-1].pos,samples[j].pos,1.8f,{.4f,.9f,1,.8f});
        if(!path && !pathBoxes)continue;
        auto prev=samples[samples.size()-2];float dt=now-prev.tick;
        auto v=(pos-prev.pos)*(1.f/dt);float ay=0;
        if(samples.size()>=3 && !p->m_isOnGround){
            auto earlier=samples[samples.size()-3];auto oldDt=prev.tick-earlier.tick;
            ay=2.f*(v.y-(prev.pos.y-earlier.pos.y)/oldDt)/(dt+oldDt);
        }
        auto last=pos;
        for(int t=2;t<=60;t+=2){
            auto next=pos+v*float(t)+CCPoint{0,.5f*ay*t*(t+dt)};
            auto box=inner;box.origin=box.origin+(next-pos);
            bool hit=std::any_of(hazards.begin(),hazards.end(),[&](auto const& r){return box.intersectsRect(r);});
            node->drawSegment(last,next,1,hit?red:blue);
            if(pathBoxes || hit)outline(node,box,hit?red:blue);
            if(hit)break;
            last=next;
        }
    }
}
}
class $modify(CFToolParticles,CCParticleSystemQuad){
    void draw(){if(!hasLiveOwner() || !tool("tool-no-particles"))CCParticleSystemQuad::draw();}
};
class $modify(CFToolBatchParticles,CCParticleBatchNode){
    void draw(){if(!hasLiveOwner() || !tool("tool-no-particles"))CCParticleBatchNode::draw();}
};
class $modify(CFToolTrail,CCMotionStreak){
    void draw(){if(hasLiveOwner() && tool("tool-no-trail") && !tool("tool-force")){
            for(auto p:{s.owner->m_player1,s.owner->m_player2})
                if(p && (p->m_regularTrail==static_cast<CCMotionStreak*>(this) || p->m_shipStreak==static_cast<CCMotionStreak*>(this)))return;
        }
        CCMotionStreak::draw();}
};
class $modify(CFToolWave,HardStreak){
    void updateStroke(float dt){
        if(!hasLiveOwner()){HardStreak::updateStroke(dt);return;}
        if(tool("tool-force")){HardStreak::updateStroke(dt);return;}
        if(tool("tool-no-wave")){clear();return;}
        auto width=m_waveSize,pulse=m_pulseSize;auto solid=m_isSolid;
        m_waveSize*=Mod::get()->getSettingValue<double>("tool-width");
        if(tool("tool-solid-wave")){m_pulseSize=1;m_isSolid=true;}
        HardStreak::updateStroke(dt);m_waveSize=width;m_pulseSize=pulse;m_isSolid=solid;
    }
};
namespace {
class ToolsHud : public CCNode {
public:
    CCLabelBMFont* label = nullptr;
    CCDrawNode* indicator = nullptr;

    static ToolsHud* create() {
        auto hud = new ToolsHud;
        if (!hud->init()) { delete hud; return nullptr; }
        hud->autorelease();
        return hud;
    }

    bool init() override {
        if (!CCNode::init()) return false;
        label = CCLabelBMFont::create("", "bigFont.fnt");
        indicator = CCDrawNode::create();
        if (!label || !indicator) return false;
        // Bitmap fonts batch glyph sprites. Keep vector drawing on a sibling.
        label->setAnchorPoint({0, 0});
        addChild(label);
        addChild(indicator);
        return true;
    }
};
}
class $modify(CFToolDisplay,CCDirector){
    void drawScene(){
        if(hasLiveOwner()){
            syncToolProtection();
            auto mod=Mod::get();double speed=mod->getSettingValue<double>("tool-speed");
            if(!running() && !s.owner->m_isPaused && speed!=1){
                if(!s.speedOwned)s.priorToolScale=getScheduler()->getTimeScale();
                s.speedOwned=true;getScheduler()->setTimeScale(speed);
            }else if(s.speedOwned){getScheduler()->setTimeScale(s.priorToolScale);s.speedOwned=false;}
            drawGuides();
            auto hud=static_cast<ToolsHud*>(s.owner->getChildByID("cf-tool-hud"));
            bool fps=tool("tool-fps"),time=tool("tool-time"),dot=tool("tool-dot");
            if(fps || time || dot){
                using Clock=std::chrono::steady_clock;static auto last=Clock::now();static int count=0;static double rate=0;
                auto now=Clock::now();double elapsed=std::chrono::duration<double>(now-last).count();
                ++count;if(elapsed*1000>=mod->getSettingValue<double>("tool-refresh")){rate=count/elapsed;count=0;last=now;}
                if(!hud){
                    hud=ToolsHud::create();
                    if(!hud){CCDirector::drawScene();return;}
                    hud->setID("cf-tool-hud");s.owner->addChild(hud,1001);
                }
                auto label=hud->label;
                float x=mod->getSettingValue<double>("tool-x")/100., y=mod->getSettingValue<double>("tool-y")/100.;
                hud->setScale(.32f*mod->getSettingValue<double>("tool-scale"));
                auto opacity=static_cast<GLubyte>(2.55*mod->getSettingValue<double>("tool-opacity"));
                label->setOpacity(opacity);hud->setAnchorPoint({x,y});hud->setPosition({x*getWinSize().width,y*getWinSize().height});
                std::string value;
                if(fps){value+=tool("tool-decimals")?fmt::format("{:.1f} FPS  ",rate):fmt::format("{:.0f} FPS  ",rate);
                    if(tool("tool-ms") && rate>0)value+=fmt::format("{:.2f} ms  ",1000/rate);}
                if(time){
                    auto partial=s.owner->m_startPosObject || s.owner->m_isTestMode || s.owner->m_isPlatformer ||
                        s.owner->m_gameState.m_timeWarp!=1.f;
                    if(!s.hudDuration)
                        s.hudDuration=cf::leveltime::total(savedLevelTicks(),tick(),s.owner->getCurrentPercent(),partial,portalEstimateTicks());
                    value+=cf::leveltime::progressClock(s.hudDuration,s.owner->m_hasCompletedLevel?100.:s.owner->getCurrentPercent(),
                        tick(),partial,tool("tool-time-total"),tool("tool-time-decimals"));

                }
                label->setString(value.c_str());
                float inset = dot ? 20.f : 0.f;
                float height = std::max(dot ? 14.f : 0.f, label->getContentSize().height);
                hud->setContentSize({inset + label->getContentSize().width, height});
                label->setPosition({inset, 0});
                auto indicator=hud->indicator;
                indicator->clear();
                if(dot){bool red=toolsNoclip() || speed!=1;
                    indicator->drawDot({7,height*.5f},6,red?ccColor4F{1,.2f,.2f,opacity/255.f}:ccColor4F{.2f,1,.4f,opacity/255.f});}
            }else if(hud)hud->removeFromParent();
        }
        CCDirector::drawScene();
    }
};
