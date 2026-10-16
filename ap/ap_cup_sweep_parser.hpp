// Included after the common seed rejection helpers in ap_seedcfg.cpp.
static bool parse_cup_sweeps(const nlohmann::json &j,const nlohmann::json &opt,int schema)
{
    auto flag=opt.find("cup_sweep_checks");
    const bool block=j.contains("cup_sweep_checks");
    if(flag==opt.end() && !block)return true;
    try {
        if(flag==opt.end() || !flag->is_boolean())throw 0;
        if(!flag->get<bool>()) {if(block)throw 0;return true;}
        if(schema<18 || !block)throw 0;
        const auto &b=j.at("cup_sweep_checks");
        if(!b.is_object() || b.size()!=2 || !b.contains("version") || !b["version"].is_number_integer() ||
           b["version"]!=1 || !b.contains("cups") || !b["cups"].is_object())throw 0;
        AP_CupSweepConfig parsed={};parsed.enabled=1;
        int expected=0;
        for(int c=0;c<5;++c) {
            const std::string key=std::to_string(100+c);
            const bool displaced=(ctr_cfg.custom_tracks_ok && ctr_cfg.custom_track.replaces_cup_level_id==100+c) ||
                                 (ctr_cfg.cortex_track.valid && ctr_cfg.cortex_track.dropped_destination==100+c);
            if(displaced) {if(b["cups"].contains(key))throw 0;continue;}
            ++expected;
            const auto &r=b["cups"].at(key);
            if(!r.is_object() || r.size()!=2 || !r.contains("location") || !r["location"].is_number_integer() ||
               r["location"]!=35028300+c || !r.contains("legs") || !r["legs"].is_array() || r["legs"].size()!=4)throw 0;
            parsed.present[c]=1;parsed.locations[c]=35028300+c;
            for(int leg=0;leg<4;++leg) {
                const auto &v=r["legs"][leg];
                if(!v.is_number_integer() || v!=ctr_cfg.gem_cup_legs[c][leg] ||
                   (!(v>=0 && v<=15) && v!=110))throw 0;
                parsed.legs[c][leg]=v.get<int>();
            }
        }
        if((int)b["cups"].size()!=expected)throw 0;
        ctr_cup_sweeps=parsed;return true;
    } catch(...) {
        hit_reject("cup_sweep_checks: inconsistent option, Cup identities or ordered legs");return false;
    }
}
