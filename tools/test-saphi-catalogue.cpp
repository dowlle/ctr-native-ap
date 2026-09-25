// g++ -std=c++17 -Wall -Wextra -Werror -I include -I ap/vendor/json/include
// tools/test-saphi-catalogue.cpp platform/native_saphi_catalogue.cpp -o /tmp/test-saphi-catalogue
#include <platform/native_saphi_catalogue.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
using json = nlohmann::json;
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while(0)
static json fixture()
{
    json lev={{"id",11},{"type","lev"},{"version","1.0.0"},{"modes",{"arcade"}},
        {"file_size",4096},{"crc32_hash",4294967295u},{"download_url","/api/v3/tracks/7/downloads/11"},{"is_current",true}};
    json vrm=lev;vrm["id"]=12;vrm["type"]="vrm";vrm["download_url"]="/api/v3/tracks/7/downloads/12";
    return {{"data",json::array({{{"id",7},{"name","Fixture Track"},{"author","Creator"},{"type","race"},
        {"is_active",true},{"lap_count",3},{"downloads",{lev,vrm}}}})}};
}
static int parse(const json &j, CustomSaphiCatalogue **out)
{
    char error[256];auto bytes=j.dump();
    return CustomSaphi_ParseCatalogue(bytes.data(),bytes.size(),out,error,sizeof error);
}
int main(int argc,char **argv)
{
    CustomSaphiCatalogue *c=nullptr;
    if(argc==2)
    {
        std::ifstream in(argv[1]);std::string s((std::istreambuf_iterator<char>(in)),{});char error[256];
        if(!CustomSaphi_ParseCatalogue(s.data(),s.size(),&c,error,sizeof error)) { std::puts(error);return 1; }
        size_t current=0, unavailable=0;
        for(size_t i=0;i<CustomSaphi_Count(c);i++) {auto *r=CustomSaphi_Row(c,i);current+=r->current;unavailable+=!!r->disabledReason[0];}
        std::printf("Live catalogue: %zu revisions, %zu current, %zu unavailable pairs\n",CustomSaphi_Count(c),current,unavailable);
        CustomSaphi_Free(&c);return 0;
    }
    auto j=fixture();CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==1);
    auto r=*CustomSaphi_Row(c,0);CHECK(r.trackID==7 && r.lev.id==11 && r.vrm.id==12);
    CHECK(r.lev.crc32==UINT32_MAX && r.sourceLaps==3 && r.current && !r.disabledReason[0]);
    CHECK(!CustomSaphi_Row(c,1));CHECK(!parse(j,&c));CHECK(CustomSaphi_Count(c)==1);CustomSaphi_Free(&c);
    j["data"][0]["downloads"][1]["modes"]={"time_trial"};
    CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(CustomSaphi_Row(c,0)->disabledReason[0] && CustomSaphi_Row(c,1)->disabledReason[0]);CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["downloads"].push_back(j["data"][0]["downloads"][0]);CHECK(!parse(j,&c));CHECK(!c);
    j=fixture();j["data"].push_back(j["data"][0]);CHECK(!parse(j,&c));
    for(const char *url:{"https://evil.test/api/v3/tracks/7/downloads/11","/api/v3/tracks/8/downloads/11", "//www.projectsaphi.com/x"})
    {j=fixture();j["data"][0]["downloads"][0]["download_url"]=url;CHECK(!parse(j,&c));}
    j=fixture();j["data"][0]["downloads"][0]["download_url"]="https://www.projectsaphi.com/api/v3/tracks/7/downloads/11";
    CHECK(parse(j,&c));CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["downloads"][0]["file_size"]=9*1024*1024;CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"][0]["crc32_hash"]=-1;CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["downloads"][0]["file_size"]=0;j["data"][0]["downloads"][0]["download_url"]=nullptr;
    CHECK(parse(j,&c));CHECK(CustomSaphi_Row(c,0)->disabledReason[0]);CHECK(!CustomSaphi_Row(c,0)->lev.path[0]);CustomSaphi_Free(&c);
    j=fixture();j["data"][0]["name"]="Bad\nTitle";CHECK(!parse(j,&c));
    j=fixture();j["data"][0]["is_active"]=false;CHECK(parse(j,&c));CHECK(!CustomSaphi_Count(c));CustomSaphi_Free(&c);
    /* Track audio: one current .sca goes to the current revision only; none,
       two current, an old one, or a bad row leaves it off without failing. */
    auto withSca=[](std::initializer_list<json> scas){
        auto j=fixture();auto old=j["data"][0]["downloads"][0];old["id"]=9;old["version"]="0.9.0";old["is_current"]=false;
        old["download_url"]="/api/v3/tracks/7/downloads/9";j["data"][0]["downloads"].push_back(old);
        auto oldv=old;oldv["id"]=10;oldv["type"]="vrm";oldv["download_url"]="/api/v3/tracks/7/downloads/10";j["data"][0]["downloads"].push_back(oldv);
        for(const auto &s:scas) j["data"][0]["downloads"].push_back(s);
        return j;};
    json sca={{"id",13},{"type","sca"},{"version","1.0.0"},{"modes",json::array()},{"file_size",383176},{"crc32_hash",3314522919u},
        {"download_url","/api/v3/tracks/7/downloads/13"},{"is_current",true}};
    auto rowFor=[](CustomSaphiCatalogue *cat,const char *version){for(size_t i=0;i<CustomSaphi_Count(cat);i++) if(!std::strcmp(CustomSaphi_Row(cat,i)->version,version)) return CustomSaphi_Row(cat,i); return (const CustomSaphiRevision*)nullptr;};
    j=withSca({sca});CHECK(parse(j,&c));CHECK(CustomSaphi_Count(c)==2);
    CHECK(rowFor(c,"1.0.0")->sca.id==13 && rowFor(c,"1.0.0")->sca.bytes==383176 && rowFor(c,"1.0.0")->sca.crc32==3314522919u);
    CHECK(!std::strcmp(rowFor(c,"1.0.0")->sca.path,"/api/v3/tracks/7/downloads/13"));
    CHECK(rowFor(c,"0.9.0")->sca.id==0 && !rowFor(c,"0.9.0")->disabledReason[0]);CustomSaphi_Free(&c);
    j=withSca({});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);
    {auto second=sca;second["id"]=14;second["download_url"]="/api/v3/tracks/7/downloads/14";
     j=withSca({sca,second});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0 && !rowFor(c,"1.0.0")->disabledReason[0]);CustomSaphi_Free(&c);
     second["is_current"]=false;j=withSca({sca,second});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==13);CustomSaphi_Free(&c);}
    {auto bad=sca;bad["download_url"]="https://evil.test/x";j=withSca({bad});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto big=sca;big["file_size"]=2*1024*1024;j=withSca({big});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto ref=sca;ref["download_url"]=nullptr;ref["file_size"]=0;j=withSca({ref});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    {auto odd=sca;odd["is_current"]="yes";j=withSca({odd});CHECK(parse(j,&c));CHECK(rowFor(c,"1.0.0")->sca.id==0);CustomSaphi_Free(&c);}
    std::printf("Saphi catalogue: %d checks, %d failures\n",checks,failures);return failures!=0;
}
