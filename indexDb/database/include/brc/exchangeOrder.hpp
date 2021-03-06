#pragma once



#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <brc/exception.hpp>
#include <brc/types.hpp>
#include <libdevcore/Log.h>
#include <libdevcore/CommonJS.h>

#include <brc/objects.hpp>

#include <brc/database.hpp>

namespace dev {
    namespace brc {
        namespace ex {

            class exchange_plugin {
            public:
                exchange_plugin() : db(nullptr) {
                }

                ~exchange_plugin();
                exchange_plugin(const boost::filesystem::path &data_dir);


                exchange_plugin(const exchange_plugin &) = default;
                exchange_plugin(exchange_plugin&&) = default;
                exchange_plugin& operator=(exchange_plugin&&) = default;
                exchange_plugin& operator=(const exchange_plugin&) = default;


                /// insert operation .
                /// \param orders   vector<order>
                /// \param reset    if true , this operation will reset, dont record.
                /// \param throw_exception  if true  throw exception when error.
                /// \return
                std::vector<result_order> insert_operation(const std::vector<order> &orders, bool reset = true, bool throw_exception = false) ;

                /// get exchange order by address,
                /// \param addr   Address
                /// \return         complete order.
                std::vector<exchange_order> get_order_by_address(const Address &addr) const;

                /// get current all orders on exchange by size. this search by id in function.
                /// \param size  once get once.
                /// \return
                std::vector<exchange_order> get_orders(uint32_t size = 50) const;

                /// get newest result_order by size
                /// \param size         once get size.
                /// \return             vector<result_orders>.
                std::vector<result_order> get_result_orders_by_news(uint32_t size = 50) const;


                /// get result_order of address by time, size must < 50
                /// \param addr         which Address
                /// \param min_time     min time
                /// \param max_time     max time
                /// \param max_size     once get size.
                /// \return             result_order
                std::vector<result_order> get_result_orders_by_address(const Address &addr, int64_t min_time, int64_t max_time, uint32_t max_size = 50) const;


                /// get exchange order by type (sell or buy && BRC or  FUEL)
                /// \param type         sell or buy
                /// \param token_type   BRC OR FUEL
                /// \param size         once search size.
                /// \return             complete order.
                std::vector<exchange_order> get_order_by_type(order_type type, order_token_type token_type, uint32_t size) const;

                std::vector<order> exits_trxid(const h256  &trxid);

                /// rollback before packed block.
                /// \return
                bool rollback();

                bool rollback_until(const h256 &block_hash, const h256 &root_hash);


                /// @brief same as commit()
                /// \param version
                /// \param block_hash
                /// \param root_hash
                void new_session(int64_t version, const h256 &block_hash, const h256& root_hash);


                ///  commit this state by block number. this function dont commit to disk. if close appliction, uncommit session will remove .
                /// \param version  block number
                /// \return  true
                bool commit(int64_t version, const h256 &block_hash, const h256& root_hash);

                /// commit to session to disk.
                /// \param version
                /// \return
                bool commit_disk(int64_t version, bool first_commit = false);


                /// deprecate all session.
                /// \return
                bool remove_all_session();



                ///
                /// \param os vector transactions id
                /// \param reset   if true, this operation rollback
                /// \return        vector<orders>
                std::vector<order>  cancel_order_by_trxid(const std::vector<h256> &os, bool reset);


                inline std::string check_version(bool p) const{
//                    const auto &obj = get_dynamic_object();
////#ifndef NDEBUG
//#if 0
//                    std::string ret = " version : " + std::to_string(obj.version)
//                                    + " block hash: " + toHex(obj.block_hash)
//                                    + " state root: " + toHex(obj.root_hash)
//                                    + " orders: " + std::to_string(obj.orders)
//                                    + " ret_orders:" + std::to_string(obj.result_orders)
//                             ;
//#else
//                    std::string ret = " version : " + std::to_string(obj.version)
//                                    + " block hash: " + (obj.block_hash.abridged())
//                                    + " state root: " + (obj.root_hash.abridged())
//                                    + " orders: " + std::to_string(obj.orders)
//                                    + " ret_orders:" + std::to_string(obj.result_orders)
//                             ;
//#endif
//                    if(p){
//                        cwarn << ret;
//                    }
                    return "";
                };
            private:

                /// get iterator by type and price . this only find order of sell.
                /// \param token_type   BRC OR FUEL
                /// \param price        upper price.
                /// \return         std::pair<lower iterator, upper iterator>
                auto get_buy_itr(order_token_type find_token, u256 price) {
//                    auto find_token = token_type == order_token_type::BRC ? order_token_type::FUEL : order_token_type::BRC;
                    const auto &index_greater = db->get_index<order_object_index>().indices().get<by_price_less>();

                    auto find_lower = boost::tuple<order_type, order_token_type, u256, Time_ms>(order_type::sell, find_token,
                                                                                                u256(0), 0);
                    auto find_upper = boost::tuple<order_type, order_token_type, u256, Time_ms>(order_type::sell, find_token,
                                                                                                price, INT64_MAX);

                    typedef decltype(index_greater.lower_bound(find_lower)) Lower_Type;
                    typedef decltype(index_greater.upper_bound(find_upper)) Upper_Type;

                    return std::pair<Lower_Type, Upper_Type>(index_greater.lower_bound(find_lower),
                                                             index_greater.upper_bound(find_upper));
                };

                /// get iterator by type and price, this only find of buy
                /// \param token_type   BRC OR FUEL,
                /// \param price        lower price.
                /// \return             std::pair<lower iterator, upper iterator>
                auto get_sell_itr(order_token_type find_token, u256 price) {
//                    auto find_token = token_type == order_token_type::BRC ? order_token_type::FUEL : order_token_type::BRC;
                    const auto &index_less = db->get_index<order_object_index>().indices().get<by_price_greater>();  //↑

                    auto find_lower = boost::tuple<order_type, order_token_type, u256, Time_ms>(order_type::buy, find_token,
                                                                                                u256(-1), 0);
                    auto find_upper = boost::tuple<order_type, order_token_type, u256, Time_ms>(order_type::buy, find_token,
                                                                                                price, INT64_MAX);

                    typedef decltype(index_less.lower_bound(find_lower)) Lower_Type;
                    typedef decltype(index_less.upper_bound(find_upper)) Upper_Type;

                    return std::pair<Lower_Type, Upper_Type>(index_less.lower_bound(find_lower),
                                                             index_less.upper_bound(find_upper));
                };


                /// process buy or sell orders by price and amount,
                /// \tparam BEGIN   get_buy_itr.first or get_sell_itr.first.
                /// \tparam END     get_buy_itr.second or get_buy_itr.second.
                /// \param begin    begin iterator,
                /// \param end      end iterator.
                /// \param od       source order.
                /// \param price    price,
                /// \param amount   exchange amount
                /// \param result   result of success order.
                /// \param throw_exception  if true, will throw exception while error.
                template<typename BEGIN, typename END>
                void process_only_price(BEGIN &begin, END &end, const order &od, const u256 &price, const u256 &amount,
                              std::vector<result_order> &result, bool throw_exception);



                //only for public interface.
                //mabey remove it.  if use for debug.
                inline void check_db() const{
#ifndef NDEBUG
                    if (!db) {
                        BOOST_THROW_EXCEPTION(get_db_instance_error());
                    }
#endif
                }

                const dynamic_object &get_dynamic_object() const ;

                ///
                /// \param up  if true ++ , false --.
                void update_dynamic_orders(bool up);

                void update_dynamic_result_orders();


            //--------------------- members ---------------------
                /// database
                std::shared_ptr<database> db;
                bool                _new_session = false;
            };



        }
    }
}
