#define BOOST_TEST_MODULE testTree
#define BOOST_TEST_DYN_LINK
//#include <boost/test/unit_test.hpp>
#include <boost/test/included/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/random.hpp>
#include <boost/format.hpp>


#include <brc/database.hpp>
#include <brc/exchangeOrder.hpp>
#include <brc/exchangeOrder.hpp>
#include <libbrcdchain/bplusTree.h>
#include <leveldb/db.h>
#include <libdevcore/dbfwd.h>
#include <libdevcore/Log.h>
#include <thread>
#include <set>

namespace bbfs = boost::filesystem;
using namespace dev;


struct virtualDb : public dev::brc::databaseDelegate {

    virtualDb(leveldb::DB *db) : m_db(db) {

    }

    virtual dev::brc::DataPackage getData(const dev::brc::DataKey &nk) {
        if (m_db) {
            leveldb::ReadOptions wo;
            std::string ret;
            auto status = m_db->Get(wo, nk, &ret);
            auto return_r = dev::brc::DataPackage(ret.c_str(), ret.c_str() + ret.size());
            return return_r;
        }

        return dev::brc::DataPackage();
    }

    virtual void setData(const dev::brc::DataKey &nk, const dev::brc::DataPackage &dp) {
        if (m_db) {
            if (!dp.size()) {
                assert(false);
            }
            leveldb::WriteOptions wo;
            auto writeValue = (dev::db::Slice) dev::ref(dp);
            m_db->Put(wo, nk, leveldb::Slice(writeValue.data(), writeValue.size()));
        }
    }

    virtual void deleteKey(const dev::brc::DataKey &nk) {
        if (m_db) {
            leveldb::WriteOptions wo;
            m_db->Delete(wo, nk);
        }
    }

private:
    leveldb::DB *m_db;
};


struct books {
    uint32_t hot = 0;
    uint32_t id = 0;

    void decode(const dev::RLP &rlp) {
        if (rlp.isList()) {
            assert(rlp.itemCount() == 2);
            hot = rlp[0].toInt<uint32_t>();
            id = rlp[1].toInt<uint32_t>();
        }

    }

    void encode(dev::RLPStream &rlp) const{
        rlp.appendList(2);
        rlp.append(hot);
        rlp.append(id);
    }

    bool operator<(const books &b2) const {
        if (hot < b2.hot) {
            return true;
        } else if (hot == b2.hot) {
            if (id < b2.id) {
                return true;
            }
        }
        return false;
    }


    std::string to_string() const {
        return "[ " + std::to_string(hot) + "-" + std::to_string(id) + "]";
    }


};

struct detailbook {
    std::string name;
    std::string from;

    void decode(const dev::RLP &rlp) {
        assert(rlp.itemCount() == 2);
        name = rlp[0].toString();
        from = rlp[1].toString();
    }

    void encode(dev::RLPStream &rlp) const{
        rlp.appendList(2);
        rlp.append(name);
        rlp.append(from);

    }

    std::string to_string() {
        return "[ " + (name) + "-" + (from) + "]";
    }

};

 
struct test_op {
public:
    bool operator > (const test_op &t1) const {
        if(first > t1.first){
            return true;
        }
        else if (first == t1.first){
            return second > t1.second;
        }
        return false;
    }

    bool operator < (const test_op &t1) const {
        if(first < t1.first){
            return true;
        }
        else if (first == t1.first){
            return second < t1.second;
        }
        return false;
    }

    bool operator == (const test_op &t1) const {
        return first == t1.first && second == t1.second;
    }

     bool operator != (const test_op &t1) const {
        return first != t1.first || second != t1.second;
    }

    void encode(dev::RLPStream &rlp) const
    {

    }

    void decode(dev::RLP const& _rlp)
    {

    } 

    std::string to_string() const
    {
        return "[" + std::to_string(first) + "," + std::to_string(second) + "]";
    }

    // bool operator () (const unsigned &t1, const unsigned &t2) const {
    //     return t1 < t2;
    // }

    int32_t first;
    int32_t second;
};


std::vector<int32_t> getRand(int32_t min, int32_t max, int32_t limit, int sed = 0){
    if(sed == 0){
        auto seed = time(0);
        srand(seed);
    }else{
        srand(sed);
    }

    std::vector<int32_t> data;

    for(size_t i = 0; i < limit;){
        int32_t ret = rand();
        ret %= (max - min);
        ret += min;
        data.push_back(ret);
        i++;
    }
    return data;
}

std::vector<int32_t> get_diff_Rand(int32_t min, int32_t max, int32_t limit, int sed = 0){
    if(sed == 0){
        auto seed = time(0);
        std::cout << "seed #### " << seed << std::endl;
        srand(seed);
    }else{
        srand(sed);
    }
    assert(max - min > limit);

    std::vector<int32_t> data;

    for(size_t i = 0; i < limit;){
        int32_t ret = rand();
        ret %= (max - min);
        ret += min;
        if(data.end() == std::find(data.begin(), data.end(), ret)){
            data.push_back(ret);
            i++;
        }
      
    }
    return data;
}


BOOST_AUTO_TEST_SUITE(testTree)

    BOOST_AUTO_TEST_CASE(tree_test1) {
        try {


            dev::brc::bplusTree<unsigned, std::string, 4> bp;
            size_t end = 128;
            for (size_t i = 0; i < end; i++) {
                bp.insert(i, std::to_string(i));
            }
            bp.debug();


        } catch (const std::exception &e) {

        } catch (const boost::exception &e) {

        } catch (...) {

        }
    }

    BOOST_AUTO_TEST_CASE(tree_code) {
        try {
            leveldb::Options op;
            op.create_if_missing = true;
            op.max_open_files = 256;


            auto db = static_cast<leveldb::DB *>(nullptr);
            auto ret = leveldb::DB::Open(op, "tdb", &db);
            std::cout << "open db : " << ret.ok() << std::endl;

            {
                std::shared_ptr<virtualDb> vdb(new virtualDb(db));
                dev::brc::bplusTree<unsigned, std::string, 4> bp(vdb);

                bp.debug();
                bp.update();
            }

        } catch (const std::exception &e) {

        } catch (const boost::exception &e) {

        } catch (...) {

        }
    }

    BOOST_AUTO_TEST_CASE(tree_code3) {

        try {
            leveldb::Options op;
            op.create_if_missing = true;
            op.max_open_files = 256;


            auto db = static_cast<leveldb::DB *>(nullptr);
            auto ret = leveldb::DB::Open(op, "tdb", &db);
            std::cout << "open db : " << ret.ok() << std::endl;

            {
                std::shared_ptr<virtualDb> vdb(new virtualDb(db));

                dev::brc::bplusTree<books, detailbook, 4> bp(vdb);

                size_t end = 32;
                for (size_t i = 0; i < end; i++) {
                    books b;
                    b.hot = i;
                    b.id = i;
                    detailbook db;
                    db.name = std::to_string(i);
                    db.from = std::to_string(i);
                    bp.insert(b, db);
                }
                bp.debug();
                bp.update();
            }


            {
                std::shared_ptr<virtualDb> vdb(new virtualDb(db));
                dev::brc::bplusTree<books, detailbook, 4> bp(vdb);
                bp.debug();
                bp.update();
            }

        } catch (const std::exception &e) {
            std::cout << "std e " << e.what() << std::endl;
        } catch (const boost::exception &e) {
        } catch (...) {
            std::cout << "std e xxx" << std::endl;
        }



    }


    BOOST_AUTO_TEST_CASE(tree_iter) {
        try {
           
            auto rand_number = [](int32_t min, int32_t max, size_t size, int se = 0) -> std::set<std::pair<int32_t, int32_t>> {
                if(se == 0){
                    auto seed = time(0);
                    srand(seed);
                    std::cout << "seed  " << seed << std::endl;
                }else{
                     srand(se);
                }
               
                //  srand(10);
                 std::set<std::pair<int32_t, int32_t>> data;
                 for(size_t i = 0; i < size;){
                    int32_t ret = rand();
                    ret %= (max - min);
                    ret += min;

                    int32_t ret1 = rand();
                    ret1 %= (max - min);
                    ret1 += min;

                    if(!data.count({ret, ret1})){
                        i++;
                        data.insert({ret, ret1});
                    }
                   
                 }
                
                 return data;
            };
            int number = 0;
                dev::brc::bplusTree<test_op, std::string, 4, std::less<test_op>> bp;
                auto data = rand_number(1, 200, 1000);

                for(auto &itr : data){
                     bp.insert( {itr.first, itr.second}, "11");
                }

                std::cout << "sleep 1s" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                bp.debug();

                std::cout << bp.getKeysStr() << std::endl;

                size_t i = 0;

                auto data2 = rand_number(1, 200, 1000);

                for(auto &itr : data2){
                    if(i == 172){
                        int ww = 0;
                    }
                    std::cout << "remove ======================== " << i << " data1: " << itr.first << "  data:" << itr.second << std::endl;
                    if(bp.remove( {itr.first, itr.second})){
                        data.erase({itr.first, itr.second});

                        // bp.debug();
                        bp.update();
                       

                        //find key exits
                        for(auto &it : data){ 
                            if(!bp.find_key({it.first, it.second})){
                                std::cout << "cant find key " <<  i << " data " << it.first << "," << it.second << std::endl;
                                assert(false);
                            }
                        }
                    }
                    i++;
                }
                bp.debug();
                
        } catch (const std::exception &e) {
            std::cout << "exception " << e.what() << std::endl;
        } catch (const boost::exception &e) {

        } catch (...) {

        }
    }



    BOOST_AUTO_TEST_CASE(tree_iter2) {
        try {
            int32_t num = 0;
            while(num++ < 1000){
            std::cout << "begin =============  " <<  num << "===== \n";
            auto rand_number1 = get_diff_Rand(1, 1000, 500);

            std::set<int32_t> ex_data;


            dev::brc::bplusTree<test_op, std::string, 4, std::less<test_op>> bp;

            for(auto &itr : rand_number1){
                bp.insert({itr, itr}, "##");
                ex_data.insert(itr);
            }
            // bp.debug();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            auto rm = get_diff_Rand(0, 500, 400);

            for(auto &itr : rm){
                // std::cout << "remove ========== index " << itr << "  key: " << rand_number1[itr] << std::endl;
                if(bp.remove({rand_number1[itr], rand_number1[itr]})){
                    bp.update();
                    ex_data.erase(rand_number1[itr]);

                    auto ex_begin = ex_data.begin();
                    auto bp_begin = bp.begin();
                    while(ex_begin != ex_data.end() && bp_begin != bp.end()){
                        if(test_op{*ex_begin, *ex_begin} != (*bp_begin).first){
                            assert(false);
                        }
                        ex_begin++;
                        bp_begin++;
                    }
                    assert(ex_begin == ex_data.end() && bp_begin == bp.end());
                }
            }

            // bp.debug();
        
            }

                
        } catch (const std::exception &e) {
            std::cout << "exception " << e.what() << std::endl;
        } catch (const boost::exception &e) {

        } catch (...) {

        }
    }





BOOST_AUTO_TEST_SUITE_END()