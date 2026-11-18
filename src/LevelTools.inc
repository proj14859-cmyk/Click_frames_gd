#include <deque>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCMotionStreak.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCParticleBatchNode.hpp>
#include <Geode/modify/HardStreak.hpp>

namespace {
bool tool(char const* key){return Mod::get()->getSettingValue<bool>(key);}
class ToolsPopup : public Popup {
    std::vector<std::string> keys;
    std::vector<SettingNodeV3*> rows;
    CCNode* list=nullptr;
    size_t page=0;
    void commit(){
        for(auto row:rows)if(row->hasUncommittedChanges())row->commit();
        syncToolProtection();
    }
    void showPage(){
        rows.clear();list->removeAllChildren();
        for(size_t i=page*5;i<std::min(page*5+5,keys.size());++i){
            auto row=Mod::get()->getSetting(keys[i])->createNode(350);
            if(!row)continue;
            row->setAnchorPoint({0,0});row->setPosition({20,178.f-34*(i%5)});
            list->addChild(row);rows.push_back(row);
        }
    }
    void onClose(CCObject* sender) override {commit();Popup::onClose(sender);}
    bool init(){
        if(!Popup::init(390,240))return false;
        setTitle("Level tools");
        for(auto key:Mod::get()->getSettingKeys())
            if(key.starts_with("tool-") && key!="tool-section")keys.push_back(key);
        list=CCNode::create();m_mainLayer->addChild(list);
        auto menu=CCMenu::create();menu->setPosition({0,0});m_mainLayer->addChild(menu);
        for(int dir:{-1,1}){
            auto sprite=ButtonSprite::create(dir<0?"<":">");sprite->setScale(.5f);
            auto item=CCMenuItemExt::createSpriteExtra(sprite,[this,dir](auto){
                commit();page=(page+(keys.size()+4)/5+dir)%((keys.size()+4)/5);showPage();
            });
            item->setPosition({195.f+dir*44,22});menu->addChild(item);
        }
        showPage();return true;
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
    if((boxes || path || pathBoxes) && layer->m_objects){
        for(unsigned i=0;i<layer->m_objects->count();++i){
            auto obj=static_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
            if(!obj || obj->m_isDisabled || obj->m_isDisabled2)continue;
            auto closeToPlayer=[&](PlayerObject* p){return p && std::abs(p->getPositionX()-obj->getPositionX())<700 &&
                std::abs(p->getPositionY()-obj->getPositionY())<450;};
            if(!closeToPlayer(layer->m_player1) && !(layer->m_gameState.m_isDualMode && closeToPlayer(layer->m_player2)))continue;
            bool hazard=obj->m_objectType==GameObjectType::Hazard || obj->m_objectType==GameObjectType::AnimatedHazard;
            if(!hazard && obj->m_objectType!=GameObjectType::Solid && obj->m_objectType!=GameObjectType::Slope &&
                obj->m_objectType!=GameObjectType::Breakable)continue;
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
            auto label=static_cast<CCLabelBMFont*>(s.owner->getChildByID("cf-tool-hud"));
            bool fps=tool("tool-fps"),time=tool("tool-time"),dot=tool("tool-dot");
            if(fps || time || dot){
                using Clock=std::chrono::steady_clock;static auto last=Clock::now();static int count=0;static double rate=0;
                auto now=Clock::now();double elapsed=std::chrono::duration<double>(now-last).count();
                ++count;if(elapsed*1000>=mod->getSettingValue<double>("tool-refresh")){rate=count/elapsed;count=0;last=now;}
                if(!label){label=CCLabelBMFont::create("","bigFont.fnt");label->setID("cf-tool-hud");s.owner->addChild(label,1001);}
                float x=mod->getSettingValue<double>("tool-x")/100., y=mod->getSettingValue<double>("tool-y")/100.;
                label->setScale(.32f*mod->getSettingValue<double>("tool-scale"));
                auto opacity=static_cast<GLubyte>(2.55*mod->getSettingValue<double>("tool-opacity"));
                label->setOpacity(opacity);label->setAnchorPoint({x,y});label->setPosition({x*getWinSize().width,y*getWinSize().height});
                std::string value=dot?"    ":"";
                if(fps){value+=tool("tool-decimals")?fmt::format("{:.1f} FPS  ",rate):fmt::format("{:.0f} FPS  ",rate);
                    if(tool("tool-ms") && rate>0)value+=fmt::format("{:.2f} ms  ",1000/rate);}
                if(time)value+=fmt::format("{:.2f} s",tick()/240.);
                label->setString(value.c_str());
                auto indicator=static_cast<CCDrawNode*>(label->getChildByID("status-dot"));
                if(!indicator){indicator=CCDrawNode::create();indicator->setID("status-dot");label->addChild(indicator);}
                indicator->clear();
                if(dot){bool red=toolsNoclip() || speed!=1;
                    indicator->drawDot({7,14},6,red?ccColor4F{1,.2f,.2f,opacity/255.f}:ccColor4F{.2f,1,.4f,opacity/255.f});}
            }else if(label)label->removeFromParent();
        }
        CCDirector::drawScene();
    }
};
