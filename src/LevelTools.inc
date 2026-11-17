#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCMotionStreak.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/CCParticleBatchNode.hpp>
#include <Geode/modify/HardStreak.hpp>

namespace {
bool tool(char const* key){return Mod::get()->getSettingValue<bool>(key);}
void showLevelTools(){
    createQuickPopup("Level tools", "Visual tools apply immediately. Enable Safe mode for this level session to use noclip, speed, hitboxes or trajectory. Exit the level to end Safe mode.",
        "Settings","Safe mode",[](auto,bool assist){
            if(assist){
                if(!hasLiveOwner() || (s.state!=State::Idle && s.state!=State::Ready)){
                    FLAlertLayer::create("Tools","Stop analysis or recording first.","OK")->show();return;
                }
                if(!s.toolSafe){
                    captureLevelStats();s.toolSafe=true;s.owner->m_isTestMode=true;
                    s.priorToolScale=CCDirector::get()->getScheduler()->getTimeScale();
                    auto marker=CCNode::create();marker->setID("cf-assisted-attempt");s.owner->addChild(marker);
                }
            }
            openSettingsPopup(Mod::get());
        });
}
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
    if(!s.toolSafe || !(boxes || path || pathBoxes)){if(node)node->removeFromParent();return;}
    if(!node){node=CCDrawNode::create();node->setID("cf-tool-guides");layer->m_objectLayer->addChild(node,1000);}
    node->clear();
    auto player=layer->m_player1;auto pos=player->getPosition();
    if(boxes && layer->m_objects){
        for(unsigned i=0;i<layer->m_objects->count();++i){
            auto obj=static_cast<GameObject*>(layer->m_objects->objectAtIndex(i));
            if(obj && !obj->m_isDisabled && !obj->m_isDisabled2 && obj->m_objectType==GameObjectType::Solid &&
                std::abs(pos.x-obj->getPositionX())<600 && std::abs(pos.y-obj->getPositionY())<360)
                outline(node,obj->getObjectRect(),{.2f,1,.5f,.7f});
        }
    }
    if(path || pathBoxes){
        // Draw only measured simulation branches near the current input.
        for(auto const& [idx,branches]:s.predictions){
            if(idx>=s.recording.inputs.size() || std::abs(s.recording.inputs[idx].tick-tick())>120)continue;
            bool first=s.recording.inputs[idx].p1;
            auto active=first?player:layer->m_player2;if(!active)continue;
            auto rect=active->getObjectRect();
            for(auto const& branch:branches){
                ccColor4F color=branch.pass?ccColor4F{.3f,1,.6f,.7f}:ccColor4F{1,.3f,.2f,.8f};
                for(size_t i=1;i<branch.points.size();++i){
                    auto a=branch.points[i-1],b=branch.points[i];
                    CCPoint start=first?CCPoint{a.x,a.y}:CCPoint{a.x2,a.y2};
                    CCPoint end=first?CCPoint{b.x,b.y}:CCPoint{b.x2,b.y2};
                    node->drawSegment(start,end,1,color);
                    if(pathBoxes){auto box=rect;box.origin=box.origin+(end-active->getPosition());outline(node,box,color);}
                }
            }
            break;
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
    void draw(){if(!hasLiveOwner() || !tool("tool-no-trail"))CCMotionStreak::draw();}
};
class $modify(CFToolWave,HardStreak){
    void updateStroke(float dt){
        if(!hasLiveOwner()){HardStreak::updateStroke(dt);return;}
        if(tool("tool-no-trail") || tool("tool-no-wave")){clear();return;}
        auto width=m_waveSize,pulse=m_pulseSize;auto solid=m_isSolid;
        m_waveSize*=Mod::get()->getSettingValue<double>("tool-width");
        if(tool("tool-solid-wave")){m_pulseSize=1;m_isSolid=true;}
        HardStreak::updateStroke(dt);m_waveSize=width;m_pulseSize=pulse;m_isSolid=solid;
    }
};
class $modify(CFToolDisplay,CCDirector){
    void drawScene(){
        if(hasLiveOwner()){
            if(s.toolSafe && !s.owner->m_isPaused)getScheduler()->setTimeScale(Mod::get()->getSettingValue<double>("tool-speed"));
            drawGuides();
            auto label=static_cast<CCLabelBMFont*>(s.owner->getChildByID("cf-tool-hud"));
            bool fps=tool("tool-fps"),time=tool("tool-time");
            if(fps || time){
                using Clock=std::chrono::steady_clock;static auto last=Clock::now();static int count=0;static double rate=0;
                auto now=Clock::now();double elapsed=std::chrono::duration<double>(now-last).count();
                if(++count && elapsed>=.25){rate=count/elapsed;count=0;last=now;}
                if(!label){label=CCLabelBMFont::create("","bigFont.fnt");label->setID("cf-tool-hud");label->setScale(.32f);
                    label->setAnchorPoint({0,1});label->setPosition({6,getWinSize().height-6});s.owner->addChild(label,1001);}
                label->setString(((fps?fmt::format("{:.0f} FPS  ",rate):std::string{})+(time?fmt::format("{:.2f} s",tick()/240.):std::string{})).c_str());
            }else if(label)label->removeFromParent();
        }
        CCDirector::drawScene();
    }
};
