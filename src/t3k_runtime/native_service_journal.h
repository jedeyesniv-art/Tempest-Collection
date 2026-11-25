#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <string>
namespace tempest_native {
enum class NativeServiceKind:uint32_t {input,video,audio,scores};
struct NativeServiceEvent{NativeServiceKind kind{};std::array<uint32_t,8> words{};std::vector<uint32_t> data;};
class NativeServiceJournal {
public:
    std::vector<NativeServiceEvent> events;
    bool replaying()const{return replay_;}
    bool at_boundary()const{return replay_&&cursor_==events.size();}
    void begin_phase(){events.clear();cursor_=0;replay_=false;error_.clear();}
    void replay(std::vector<NativeServiceEvent> in){events=std::move(in);cursor_=0;replay_=true;error_.clear();}
    void finish_replay(){replay_=false;}
    bool exchange(NativeServiceKind kind,std::array<uint32_t,8>& words,std::vector<uint32_t>* data=nullptr){
        if(!replay_){events.push_back({kind,words,data?*data:std::vector<uint32_t>{}});return true;}
        if(cursor_==events.size()||events[cursor_].kind!=kind){error_="Native continuation service order differs";return false;}
        if(!data&&!events[cursor_].data.empty()){error_="Unexpected native service data";return false;}
        if(data)*data=events[cursor_].data;words=events[cursor_++].words;return true;
    }
    const std::string& error()const{return error_;}
private:
    size_t cursor_=0;bool replay_=false;std::string error_;
};
}
