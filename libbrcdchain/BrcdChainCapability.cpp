#include "BrcdChainCapability.h"
#include "BlockChain.h"
#include "BlockChainSync.h"
#include "BlockQueue.h"
#include "TransactionQueue.h"
#include "Block.h"
#include "Executive.h"
#include <libdevcore/Common.h>
#include <libbrccore/Exceptions.h>
#include <libp2p/Host.h>
#include <libp2p/Session.h>
#include <chrono>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::brc;

static unsigned const c_maxSendTransactions = 256;
static unsigned const c_maxHeadersToSend = 1024;
static unsigned const c_maxIncomingNewHashes = 1024;
static int const c_backroundWorkPeriodMs = 1000;

char const* const BrcdChainCapability::s_stateNames[static_cast<int>(SyncState::Size)] = {
    "NotSynced", "Idle", "Waiting", "Blocks", "State"};

namespace
{
string toString(Asking _a)
{
    switch (_a)
    {
    case Asking::BlockHeaders:
        return "BlockHeaders";
    case Asking::BlockBodies:
        return "BlockBodies";
    case Asking::NodeData:
        return "NodeData";
    case Asking::Receipts:
        return "Receipts";
    case Asking::Nothing:
        return "Nothing";
    case Asking::State:
        return "State";
    case Asking::WarpManifest:
        return "WarpManifest";
    case Asking::WarpData:
        return "WarpData";
     case Asking::UpdateStatus:
            return "UpdateStatus";
    }
    return "?";
}

class BrcdChainPeerObserver: public BrcdChainPeerObserverFace
{
public:
    BrcdChainPeerObserver(shared_ptr<BlockChainSync> _sync, TransactionQueue& _tq): m_sync(_sync), m_tq(_tq) {}

    void onPeerStatus(BrcdChainPeer const& _peer) override
    {
        try
        {
            m_sync->onPeerStatus(_peer);
        }
        catch (FailedInvariant const&)
        {
            cwarn << "Failed invariant during sync, restarting sync";
            m_sync->restartSync();
        }
    }

    void onPeerTransactions(NodeID const& _peerID, RLP const& _r, BlockChain const& _blockChain, OverlayDB const& _db, ex::exchange_plugin const& _exdb) override
    {
        dev::brc::Block _currentBlock = Block(_blockChain, _db, _exdb);
        _currentBlock.populateFromChain(_blockChain, _blockChain.currentHash());
        Executive e(_currentBlock, _blockChain);
        std::vector<bytesConstRef> _data;
        for(unsigned i = 0; i < _r.itemCount(); i++)
        {
            try{
                e.initialize(Transaction(_r[i].data(), CheckTransaction::None),transationTool::initializeEnum::rpcinitialize);
                _data.push_back(_r[i].data());
            }catch(...)
            {
                continue;
            }
        }


        if(!m_sync->isSyncing()){
            unsigned itemCount = _r.itemCount();
            LOG(m_logger) << "Transactions (" << dec << itemCount << " entries)";
            if(_data.size())
            {
                m_tq.enqueue(_data, _peerID);
            }
        }
    }

    void onPeerAborting() override
    {
        try
        {
            m_sync->onPeerAborting();
        }
        catch (Exception&)
        {
            cwarn << "Exception on peer destruciton: " << boost::current_exception_diagnostic_information();
        }
    }

    void onPeerBlockHeaders(NodeID const& _peerID, RLP const& _headers) override
    {
        try
        {
            m_sync->onPeerBlockHeaders(_peerID, _headers);
        }
        catch (FailedInvariant const&)
        {
            cwarn << "Failed invariant during sync, restarting sync";
            m_sync->restartSync();
        }
    }

    void onPeerBlockBodies(NodeID const& _peerID, RLP const& _r) override
    {
        try
        {
            m_sync->onPeerBlockBodies(_peerID, _r);
        }
        catch (FailedInvariant const&)
        {
            cwarn << "Failed invariant during sync, restarting sync";
            m_sync->restartSync();
        }
    }

    void onPeerNewHashes(
        NodeID const& _peerID, std::vector<std::pair<h256, u256>> const& _hashes) override
    {
        try
        {
            m_sync->onPeerNewHashes(_peerID, _hashes);
        }
        catch (FailedInvariant const&)
        {
            cwarn << "Failed invariant during sync, restarting sync";
            m_sync->restartSync();
        }
    }

    void onPeerNewBlock(NodeID const& _peerID, RLP const& _r) override
    {
        try
        {
            m_sync->onPeerNewBlock(_peerID, _r);
        }
        catch (FailedInvariant const&)
        {
            cwarn << "Failed invariant during sync, restarting sync";
            m_sync->restartSync();
        }
    }

    void onPeerNodeData(NodeID const& /* _peerID */, RLP const& _r) override
    {
        unsigned itemCount = _r.itemCount();
        LOG(m_logger) << "Node Data (" << dec << itemCount << " entries)";
    }

    void onPeerReceipts(NodeID const& /* _peerID */, RLP const& _r) override
    {
        unsigned itemCount = _r.itemCount();
        LOG(m_logger) << "Receipts (" << dec << itemCount << " entries)";
    }

private:
    shared_ptr<BlockChainSync> m_sync;
    TransactionQueue& m_tq;

    Logger m_logger{createLogger(VerbosityDebug, "host")};
};

class BrcdChainHostData: public BrcdChainHostDataFace
{
public:
    BrcdChainHostData(BlockChain const& _chain, OverlayDB const& _db): m_chain(_chain), m_db(_db) {}

    pair<bytes, unsigned> blockHeaders(RLP const& _blockId, unsigned _maxHeaders, u256 _skip, bool _reverse) const override
    {
        auto numHeadersToSend = _maxHeaders;

        auto step = static_cast<unsigned>(_skip) + 1;
        assert(step > 0 && "step must not be 0");

        h256 blockHash;
        if (_blockId.size() == 32)  // block id is a hash
        {
            blockHash = _blockId.toHash<h256>();
            cnetlog << "GetBlockHeaders (block (hash): " << blockHash
                    << ", maxHeaders: " << _maxHeaders << ", skip: " << _skip
                    << ", reverse: " << _reverse << ")";

            if (!m_chain.isKnown(blockHash))
                blockHash = {};
            else if (!_reverse)
            {
                auto n = m_chain.number(blockHash);
                if (numHeadersToSend == 0)
                    blockHash = {};
                else if (n != 0 || blockHash == m_chain.genesisHash())
                {
                    auto top = n + uint64_t(step) * numHeadersToSend - 1;
                    auto lastBlock = m_chain.number();
                    if (top > lastBlock)
                    {
                        numHeadersToSend = (lastBlock - n) / step + 1;
                        top = n + step * (numHeadersToSend - 1);
                    }
                    assert(top <= lastBlock && "invalid top block calculated");
                    blockHash = m_chain.numberHash(static_cast<unsigned>(top)); // override start block hash with the hash of the top block we have
                }
                else
                    blockHash = {};
            }
        }
        else // block id is a number
        {
            auto n = _blockId.toInt<bigint>();
            cnetlog << "GetBlockHeaders (" << n << " max: " << _maxHeaders << " skip: " << _skip
                    << (_reverse ? " reverse" : "") << ")";

            if (!_reverse)
            {
                auto lastBlock = m_chain.number();
                if (n > lastBlock || numHeadersToSend == 0)
                    blockHash = {};
                else
                {
                    bigint top = n + uint64_t(step) * (numHeadersToSend - 1);
                    if (top > lastBlock)
                    {
                        numHeadersToSend = (lastBlock - static_cast<unsigned>(n)) / step + 1;
                        top = n + step * (numHeadersToSend - 1);
                    }
                    assert(top <= lastBlock && "invalid top block calculated");
                    blockHash = m_chain.numberHash(static_cast<unsigned>(top)); // override start block hash with the hash of the top block we have
                }
            }
            else if (n <= std::numeric_limits<unsigned>::max())
                blockHash = m_chain.numberHash(static_cast<unsigned>(n));
            else
                blockHash = {};
        }

        auto nextHash = [this](h256 _h, unsigned _step)
        {
            static const unsigned c_blockNumberUsageLimit = 1000;

            const auto lastBlock = m_chain.number();
            const auto limitBlock = lastBlock > c_blockNumberUsageLimit ? lastBlock - c_blockNumberUsageLimit : 0; // find the number of the block below which we don't expect BC changes.

            while (_step) // parent hash traversal
            {
                auto details = m_chain.details(_h);
                if (details.number < limitBlock)
                    break; // stop using parent hash traversal, fallback to using block numbers
                _h = details.parent;
                --_step;
            }

            if (_step) // still need lower block
            {
                auto n = m_chain.number(_h);
                if (n >= _step)
                    _h = m_chain.numberHash(n - _step);
                else
                    _h = {};
            }


            return _h;
        };

        bytes rlp;
        unsigned itemCount = 0;
        vector<h256> hashes;

        for (unsigned i = 0; i != numHeadersToSend; ++i)
        {
            if (!blockHash || !m_chain.isKnown(blockHash))
                break;

            hashes.push_back(blockHash);
            ++itemCount;

            blockHash = nextHash(blockHash, step);
        }
        if(hashes.size() == 0 ||  hashes.size() != _maxHeaders){
            cnote << "request " << _blockId.toInt<bigint>() <<  "  _maxHeaders " << _maxHeaders << " blockHash " << blockHash <<  " response hashes.size() " << hashes.size();
        }

        for (unsigned i = 0; i < hashes.size() && rlp.size() < c_maxPayload; ++i)
            rlp += m_chain.headerData(hashes[_reverse ? i : hashes.size() - 1 - i]);

        return make_pair(rlp, itemCount);
    }

    pair<bytes, unsigned> blockBodies(RLP const& _blockHashes) const override
    {
        unsigned const count = static_cast<unsigned>(_blockHashes.itemCount());

        bytes rlp;
        unsigned n = 0;
        auto numBodiesToSend = std::min(count, c_maxBlocks);
        for (unsigned i = 0; i < numBodiesToSend && rlp.size() < c_maxPayload; ++i)
        {
            auto h = _blockHashes[i].toHash<h256>();
            if (m_chain.isKnown(h))
            {
                bytes blockBytes = m_chain.block(h);
                RLP block{blockBytes};
                RLPStream body;
                body.appendList(2);
                body.appendRaw(block[1].data());  // transactions
                body.appendRaw(block[2].data());  // uncles
                auto bodyBytes = body.out();
                rlp.insert(rlp.end(), bodyBytes.begin(), bodyBytes.end());
                ++n;
            }
        }
        if (count > 20 && n == 0)
            cnetlog << "all " << count << " unknown blocks requested; peer on different chain?";
        else
            cnetlog << n << " blocks known and returned; " << (numBodiesToSend - n)
                    << " blocks unknown; " << (count > c_maxBlocks ? count - c_maxBlocks : 0)
                    << " blocks ignored";

        return make_pair(rlp, n);
    }

    strings nodeData(RLP const& _dataHashes) const override
    {
        unsigned const count = static_cast<unsigned>(_dataHashes.itemCount());

        strings data;
        size_t payloadSize = 0;
        auto numItemsToSend = std::min(count, c_maxNodes);
        for (unsigned i = 0; i < numItemsToSend && payloadSize < c_maxPayload; ++i)
        {
            auto h = _dataHashes[i].toHash<h256>();
            auto node = m_db.lookup(h);
            if (!node.empty())
            {
                payloadSize += node.length();
                data.push_back(move(node));
            }
        }
        cnetlog << data.size() << " nodes known and returned; " << (numItemsToSend - data.size())
                << " unknown; " << (count > c_maxNodes ? count - c_maxNodes : 0) << " ignored";

        return data;
    }

    pair<bytes, unsigned> receipts(RLP const& _blockHashes) const override
    {
        unsigned const count = static_cast<unsigned>(_blockHashes.itemCount());

        bytes rlp;
        unsigned n = 0;
        auto numItemsToSend = std::min(count, c_maxReceipts);
        for (unsigned i = 0; i < numItemsToSend && rlp.size() < c_maxPayload; ++i)
        {
            auto h = _blockHashes[i].toHash<h256>();
            if (m_chain.isKnown(h))
            {
                auto const receipts = m_chain.receipts(h);
                auto receiptsRlpList = receipts.rlp();
                rlp.insert(rlp.end(), receiptsRlpList.begin(), receiptsRlpList.end());
                ++n;
            }
        }
        cnetlog << n << " receipt lists known and returned; " << (numItemsToSend - n)
                << " unknown; " << (count > c_maxReceipts ? count - c_maxReceipts : 0)
                << " ignored";

        return make_pair(rlp, n);
    }

private:
    BlockChain const& m_chain;
    OverlayDB const& m_db;
};

}

BrcdChainCapability::BrcdChainCapability(shared_ptr<p2p::CapabilityHostFace> _host,
    BlockChain const& _ch, OverlayDB const& _db, ex::exchange_plugin const& _exdb,TransactionQueue& _tq, BlockQueue& _bq,
    u256 _networkId)
  : m_host(move(_host)),
    m_chain(_ch),
    m_db(_db),
    m_exdb(_exdb),
    m_tq(_tq),
    m_bq(_bq),
    m_networkId(_networkId),
    m_hostData(new BrcdChainHostData(m_chain, m_db))
{
    // TODO: Composition would be better. Left like that to avoid initialization
    //       issues as BlockChainSync accesses other BrcdChainHost members.
    m_sync.reset(new BlockChainSync(*this));
    m_peerObserver.reset(new BrcdChainPeerObserver(m_sync, m_tq));
    m_latestBlockSent = _ch.currentHash();
    m_tq.onImport([this](ImportResult _ir, h256 const& _h, h512 const& _nodeId) { onTransactionImported(_ir, _h, _nodeId); });
    std::random_device seed;
    m_urng = std::mt19937_64(seed());
}

void BrcdChainCapability::onStarting()
{
    m_backgroundWorkEnabled = true;
    m_host->scheduleExecution(c_backroundWorkPeriodMs, [this]() { doBackgroundWork(); });
}

void BrcdChainCapability::onStopping()
{
    m_backgroundWorkEnabled = false;
}

bool BrcdChainCapability::ensureInitialised()
{
    if (!m_latestBlockSent)
    {
        // First time - just initialise.
        m_latestBlockSent = m_chain.currentHash();
        LOG(m_logger) << "Initialising: latest= " << m_latestBlockSent;

        m_transactionsSent = m_tq.knownTransactions();
        return true;
    }
    return false;
}

void BrcdChainCapability::reset()
{
    m_sync->abortSync();

    // reset() can be called from RPC handling thread,
    // but we access m_latestBlockSent and m_transactionsSent only from the network thread
    m_host->scheduleExecution(0, [this]() {
        m_latestBlockSent = h256();
        m_latestBlockSent = m_chain.currentHash();
        m_transactionsSent.clear();
    });
}

void BrcdChainCapability::completeSync()
{
    m_sync->completeSync();
}

void BrcdChainCapability::doBackgroundWork()
{
    try{
        ensureInitialised();
        auto h = m_chain.currentHash();
        // If we've finished our initial sync (including getting all the blocks into the chain so as to reduce invalid transactions), start trading transactions & blocks
        if (!isSyncing() && m_chain.isKnown(m_latestBlockSent))
        {
            if (m_newTransactions)
            {
                m_newTransactions = false;
                maintainTransactions();
            }
            if (m_newBlocks && h!= m_latestBlockSent)
            {
                m_newBlocks = false;
                maintainBlocks(h);
            }
        }

        time_t now = std::chrono::system_clock::to_time_t(chrono::system_clock::now());
        if (now - m_lastTick >= 1)
        {
            m_lastTick = now;
            for (auto const& peer : m_peers)
            {
                time_t now = std::chrono::system_clock::to_time_t(chrono::system_clock::now());

                if (now - peer.second.lastAsk() > 10 && peer.second.isConversing()){
                    // timeout
                    cwarn << "disconnect node : " << peer.first;
                    m_host->disconnect(peer.first, p2p::PingTimeout);
                }
            }
        }

    }
    catch (const std::exception &e){
        cwarn << "exception : " << e.what() ;
    }
    catch (const boost::exception &e){
        cwarn << "boost exception : " << boost::diagnostic_information(e);
    }
    catch (...){
        cwarn << "doBackgroundWork exception .";
    }



}

void BrcdChainCapability::maintainTransactions()
{
    // Send any new transactions.
    unordered_map<NodeID, std::vector<size_t>> peerTransactions;
    //auto ts = m_tq.topTransactions(c_maxSendTransactions);
    auto ts = m_tq.topTransactions(0, m_transactionsSent);
	{
        for (size_t i = 0; i < ts.size(); ++i)
        {
            auto const& t = ts[i];
            //bool unsent = !m_transactionsSent.count(t.sha3());
            auto peers = get<1>(randomSelection(0, [&](BrcdChainPeer const& _peer) {
                return _peer.isWaitingForTransactions() || (!_peer.isTransactionKnown(t.sha3()));
                       //(unsent && !_peer.isTransactionKnown(t.sha3()));
            }));
            for (auto const& p: peers)
                peerTransactions[p].push_back(i);
        }
        for (auto const& t: ts)
            m_transactionsSent.insert(t.sha3());
    }

	while(true)
	{
		bool _send_ok = true;
		for(auto& peer : m_peers)
		{
			bytes b;
			unsigned n = 0;
			auto _iter_index = peerTransactions[peer.first].begin();
			for(auto const& i : peerTransactions[peer.first])
			{
				peer.second.markTransactionAsKnown(ts[i].sha3());
				b += ts[i].rlp();
				++n;
				if(n >= c_maxSendTransactions)
				{
					if( (ts.begin() + i)!= ts.end())
						_send_ok = false;
					break;
				}
			}
			if(n >= peerTransactions[peer.first].size())
				n = peerTransactions[peer.first].size();
			peerTransactions[peer.first].erase(_iter_index, _iter_index + n);

			if(n || peer.second.isWaitingForTransactions())
			{
				RLPStream ts;
				m_host->prep(peer.first, name(), ts, TransactionsPacket, n).appendRaw(b, n);
				m_host->sealAndSend(peer.first, ts);
				LOG(m_logger) << "Send " << n << " transactions to " << peer.first;
			}
			//peer.second.setWaitingForTransactions(false);
		}
		for(auto& peer : m_peers)
			peer.second.setWaitingForTransactions(false);

        if( _send_ok || peerTransactions.empty())
            break;
	}

    /*
	for(auto& peer : m_peers)
	{
		bytes b;
		unsigned n = 0;
		h256 _hash = h256();
		for(auto const& i : peerTransactions[peer.first])
		{
			peer.second.markTransactionAsKnown(ts[i].sha3());
			b += ts[i].rlp();
			++n;
			if(n >= c_maxSendTransactions)
			{
				_hash = ts[i];
				break;
			}
		}


		if(n || peer.second.isWaitingForTransactions())
		{
			RLPStream ts;
			m_host->prep(peer.first, name(), ts, TransactionsPacket, n).appendRaw(b, n);
			m_host->sealAndSend(peer.first, ts);
			LOG(m_logger) << "Sent " << n << " transactions to " << peer.first;
		}
		//peer.second.setWaitingForTransactions(false);
	}*/

}

tuple<vector<NodeID>, vector<NodeID>> BrcdChainCapability::randomSelection(
    unsigned _percent, std::function<bool(BrcdChainPeer const&)> const& _allow)
{
    vector<NodeID> chosen;
    vector<NodeID> allowed;
    double percentDecimal = _percent / 100.0;

    for (auto const& peer : m_peers)
    {
        if (_allow(peer.second))
            allowed.push_back(peer.first);
    }

    if (_percent == 0 || allowed.size() == 0)
    {
        return make_tuple(move(chosen), move(allowed));
    }
    if(_percent >=100)
	{
		chosen = allowed;
		allowed.erase(allowed.begin(), allowed.end());
		return make_tuple(move(chosen), move(allowed));
	}

    std::shuffle(allowed.begin(), allowed.end(), m_urng);
    // Remove elements from the end of the shuffled allowed vector and move them to chosen.
    size_t chosenSize = percentDecimal * allowed.size();
    chosen.reserve(chosenSize);
    std::move(allowed.begin() + chosenSize, allowed.end(), std::back_inserter(chosen));
    allowed.erase(allowed.begin() + chosenSize, allowed.end());
    return make_tuple(move(chosen), move(allowed));
}

void BrcdChainCapability::maintainBlocks(h256 const& _currentHash)
{
    // Send any new blocks.
    auto detailsFrom = m_chain.details(m_latestBlockSent);
    auto detailsTo = m_chain.details(_currentHash);
    if(_currentHash == m_latestBlockSent){
        cwarn << "lastSendHash:" << m_latestBlockSent << " curr:" << _currentHash;
    }
//    if (detailsFrom.totalDifficulty < detailsTo.totalDifficulty)
    //cwarn <<" into send new block";
    {
        if (diff(detailsFrom.number, detailsTo.number) < 20)
        {
            // don't be sending more than 20 "new" blocks. if there are any more we were probably waaaay behind.
            CLATE_LOG << "Sending a new block (current is " << _currentHash << ", was "
                           << m_latestBlockSent << ")"  << " height " << detailsTo.number;

            h256s blocks = get<0>(m_chain.treeRoute(m_latestBlockSent, _currentHash, false, false, true));

            auto s = randomSelection(100, [&](BrcdChainPeer const& _peer) {
                return !_peer.isBlockKnown(_currentHash);
            });
            bool isSend = false;
            for (NodeID const& peerID : get<0>(s)){
                for (auto const& b: blocks)
                {
                    RLPStream ts;
                    m_host->prep(peerID, name(), ts, NewBlockPacket, 2)
                            .appendRaw(m_chain.block(b), 1)
                            .append(m_chain.details(b).totalDifficulty);

                    auto itPeer = m_peers.find(peerID);
                    if (itPeer != m_peers.end())
                    {
                        m_host->sealAndSend(peerID, ts);
                        itPeer->second.clearKnownBlocks();
                        isSend = true;
                    }
                }
            }

            for (NodeID const& peerID : get<1>(s))
            {
                RLPStream ts;
                m_host->prep(peerID, name(), ts, NewBlockHashesPacket, blocks.size());
                for (auto const& b: blocks)
                {
                    ts.appendList(2);
                    ts.append(b);
                    ts.append(m_chain.number(b));
                }

                auto itPeer = m_peers.find(peerID);
                if (itPeer != m_peers.end())
                {
                    m_host->sealAndSend(peerID, ts);
                    itPeer->second.clearKnownBlocks();
                    isSend = true;
                }
            }
            if(!isSend && get<0>(s).size() > 0 && get<1>(s).size() > 0){
                cwarn << "send new block error. (current is " << _currentHash << ", was "
                      << m_latestBlockSent << ")"  << " height " << detailsTo.number;
            }
        }
        m_latestBlockSent = _currentHash;
    }
}

bool BrcdChainCapability::isSyncing() const
{
    return m_sync->isSyncing();
}

SyncStatus BrcdChainCapability::status() const
{
    return m_sync->status();
}

void BrcdChainCapability::onTransactionImported(
    ImportResult _ir, h256 const& _h, h512 const& _nodeId)
{
    m_host->scheduleExecution(0, [this, _ir, _h, _nodeId]() {
        auto itPeerStatus = m_peers.find(_nodeId);
        if (itPeerStatus == m_peers.end())
            return;

        auto& peer = itPeerStatus->second;

        peer.markTransactionAsKnown(_h);
        switch (_ir)
        {
        case ImportResult::Malformed:
            m_host->updateRating(_nodeId, -100);
            break;
        case ImportResult::AlreadyKnown:
            // if we already had the transaction, then don't bother sending it on.
            m_transactionsSent.insert(_h);
            m_host->updateRating(_nodeId, 0);
            break;
        case ImportResult::Success:
            m_host->updateRating(_nodeId, 100);
            break;
        default:;
        }
    });
}

void BrcdChainCapability::onConnect(NodeID const& _peerID, u256 const& _peerCapabilityVersion)
{
    LOG(m_logger) << "on connect " << _peerID << " version : " << _peerCapabilityVersion;
    m_host->addNote(_peerID, "manners", m_host->isRude(_peerID, name()) ? "RUDE" : "nice");

    BrcdChainPeer peer{m_host, _peerID, _peerCapabilityVersion};
    m_peers.emplace(_peerID, peer);
    peer.requestStatus(m_networkId, m_chain.details().totalDifficulty, m_chain.currentHash(),
        m_chain.genesisHash(), m_chain.details().number);
}

void BrcdChainCapability::onDisconnect(NodeID const& _peerID)
{
    // TODO lower peer's rating or mark as rude if it disconnects when being asked for something
    m_peerObserver->onPeerAborting();

    m_peers.erase(_peerID);
}

bool BrcdChainCapability::interpretCapabilityPacket(
    NodeID const& _peerID, unsigned _id, RLP const& _r)
{
    auto& peer = m_peers[_peerID];
    peer.setLastAsk(std::chrono::system_clock::to_time_t(chrono::system_clock::now()));

    try
    {
        switch (_id)
        {
        case StatusPacket:
        {
            auto const peerProtocolVersion = _r[0].toInt<unsigned>();
            auto const networkId = _r[1].toInt<u256>();
            auto const totalDifficulty = _r[2].toInt<u256>();
            auto const latestHash = _r[3].toHash<h256>();
            auto const genesisHash = _r[4].toHash<h256>();
            auto const height = _r[5].toInt<u256>();

            LOG(m_logger) << "Status: " << peerProtocolVersion << " / " << networkId << " / "
                          << genesisHash << ", TD: " << totalDifficulty << " = " << latestHash << " height : " << height;

            peer.setStatus(peerProtocolVersion, networkId, totalDifficulty, latestHash, genesisHash, height);
            setIdle(_peerID);
            m_peerObserver->onPeerStatus(peer);
            break;
        }
        case TransactionsPacket:
        {
            m_peerObserver->onPeerTransactions(_peerID, _r, chain(), m_db, m_exdb);
            break;
        }
        case GetBlockHeadersPacket:
        {
            /// Packet layout:
            /// [ block: { P , B_32 }, maxHeaders: P, skip: P, reverse: P in { 0 , 1 } ]
            const auto blockId = _r[0];
            const auto maxHeaders = _r[1].toInt<u256>();
            const auto skip = _r[2].toInt<u256>();
            const auto reverse = _r[3].toInt<bool>();

            auto numHeadersToSend = maxHeaders <= c_maxHeadersToSend ?
                                        static_cast<unsigned>(maxHeaders) :
                                        c_maxHeadersToSend;

            if (skip > std::numeric_limits<unsigned>::max() - 1)
            {
                cnetdetails << "Requested block skip is too big: " << skip;
                break;
            }



            pair<bytes, unsigned> const rlpAndItemCount =
                m_hostData->blockHeaders(blockId, numHeadersToSend, skip, reverse);
            LOG(m_logger) << "GetBlockHeadersPacket " << blockId  << " maxHeaders " << maxHeaders << " skip " << skip << " reverse " << reverse
             << " response size " << rlpAndItemCount.second;
            RLPStream s;
            m_host->prep(_peerID, name(), s, BlockHeadersPacket, rlpAndItemCount.second)
                .appendRaw(rlpAndItemCount.first, rlpAndItemCount.second);
            m_host->sealAndSend(_peerID, s);
            m_host->updateRating(_peerID, 0);
            break;
        }
        case BlockHeadersPacket:
        {
            if (peer.asking() != Asking::BlockHeaders)
            {
                LOG(m_loggerImpolite)
                    << "Peer giving us block headers when we didn't ask for them.";
            }
            else
            {
                setIdle(_peerID);
                m_peerObserver->onPeerBlockHeaders(_peerID, _r);
            }
            break;
        }
        case GetBlockBodiesPacket:
        {
            unsigned count = static_cast<unsigned>(_r.itemCount());
            cnetlog << "GetBlockBodies (" << dec << count << " entries)";

            if (!count)
            {
                LOG(m_loggerImpolite) << "Zero-entry GetBlockBodies: Not replying.";
                m_host->updateRating(_peerID, -10);
                break;
            }

            pair<bytes, unsigned> const rlpAndItemCount = m_hostData->blockBodies(_r);

            m_host->updateRating(_peerID, 0);
            RLPStream s;
            m_host->prep(_peerID, name(), s, BlockBodiesPacket, rlpAndItemCount.second)
                .appendRaw(rlpAndItemCount.first, rlpAndItemCount.second);
            m_host->sealAndSend(_peerID, s);
            break;
        }
        case BlockBodiesPacket:
        {
            if (peer.asking() != Asking::BlockBodies)
                LOG(m_loggerImpolite) << "Peer giving us block bodies when we didn't ask for them.";
            else
            {
                setIdle(_peerID);
                m_peerObserver->onPeerBlockBodies(_peerID, _r);
            }
            break;
        }
        case NewBlockPacket:
        {
            m_peerObserver->onPeerNewBlock(_peerID, _r);
            break;
        }
        case NewBlockHashesPacket:
        {
            unsigned itemCount = _r.itemCount();

            cnetlog << "BlockHashes (" << dec << itemCount << " entries) "
                    << (itemCount ? "" : " : NoMoreHashes");

            if (itemCount > c_maxIncomingNewHashes)
            {
                disablePeer(_peerID, "Too many new hashes");
                break;
            }

            vector<pair<h256, u256>> hashes(itemCount);
            for (unsigned i = 0; i < itemCount; ++i)
                hashes[i] = std::make_pair(_r[i][0].toHash<h256>(), _r[i][1].toInt<u256>());

            m_peerObserver->onPeerNewHashes(_peerID, hashes);
            break;
        }
        case GetNodeDataPacket:
        {
            unsigned count = static_cast<unsigned>(_r.itemCount());
            if (!count)
            {
                LOG(m_loggerImpolite) << "Zero-entry GetNodeData: Not replying.";
                m_host->updateRating(_peerID, -10);
                break;
            }
            cnetlog << "GetNodeData (" << dec << count << " entries)";

            strings const data = m_hostData->nodeData(_r);

            m_host->updateRating(_peerID, 0);
            RLPStream s;
            m_host->prep(_peerID, name(), s, NodeDataPacket, data.size());
            for (auto const& element : data)
                s.append(element);
            m_host->sealAndSend(_peerID, s);
            break;
        }
        case GetReceiptsPacket:
        {
            unsigned count = static_cast<unsigned>(_r.itemCount());
            if (!count)
            {
                LOG(m_loggerImpolite) << "Zero-entry GetReceipts: Not replying.";
                m_host->updateRating(_peerID, -10);
                break;
            }
            cnetlog << "GetReceipts (" << dec << count << " entries)";

            pair<bytes, unsigned> const rlpAndItemCount = m_hostData->receipts(_r);

            m_host->updateRating(_peerID, 0);
            RLPStream s;
            m_host->prep(_peerID, name(), s, ReceiptsPacket, rlpAndItemCount.second)
                .appendRaw(rlpAndItemCount.first, rlpAndItemCount.second);
            m_host->sealAndSend(_peerID, s);
            break;
        }
        case NodeDataPacket:
        {
            if (peer.asking() != Asking::NodeData)
                LOG(m_loggerImpolite) << "Peer giving us node data when we didn't ask for them.";
            else
            {
                setIdle(_peerID);
                m_peerObserver->onPeerNodeData(_peerID, _r);
            }
            break;
        }
        case ReceiptsPacket:
        {
            if (peer.asking() != Asking::Receipts)
                LOG(m_loggerImpolite) << "Peer giving us receipts when we didn't ask for them.";
            else
            {
                setIdle(_peerID);
                m_peerObserver->onPeerReceipts(_peerID, _r);
            }
            break;
        }
        case GetLatestStatus:
        {
            RLPStream s;
            m_host->prep(_peerID, name(), s, UpdateStatus, 6)
                    << c_protocolVersion << m_networkId << m_chain.details().totalDifficulty << m_chain.currentHash()
                    << m_chain.genesisHash() << m_chain.details().number;
            m_host->sealAndSend(_peerID, s);
            break;
        }
        case UpdateStatus:{

            if(peer.asking() != Asking::UpdateStatus){
                LOG(m_loggerImpolite) << "Peer giving us UpdateStatus when we didn't ask for them.";
            }
            else{
                auto const peerProtocolVersion = _r[0].toInt<unsigned>();
                auto const networkId = _r[1].toInt<u256>();
                auto const totalDifficulty = _r[2].toInt<u256>();
                auto const latestHash = _r[3].toHash<h256>();
                auto const genesisHash = _r[4].toHash<h256>();
                auto const height = _r[5].toInt<u256>();
                LOG(m_logger) << "update status Status: " << peerProtocolVersion << " / " << networkId << " / "
                              << genesisHash << ", TD: " << totalDifficulty << " = " << latestHash << " height : " << height;

                peer.setStatus(peerProtocolVersion, networkId, totalDifficulty, latestHash, genesisHash, height);
                setIdle(_peerID);
                m_peerObserver->onPeerStatus(peer);
                LOG(m_logger) << " continue sync blocks.";
            }

            break;
        };
        default:
            return false;
        }
    }
    catch (Exception const&)
    {
        cnetlog << "Peer causing an Exception: "
                << boost::current_exception_diagnostic_information() << " " << _r;
    }
    catch (std::exception const& _e)
    {
        cnetlog << "Peer causing an exception: " << _e.what() << " " << _r;
    }

    return true;
}

void BrcdChainCapability::setIdle(NodeID const& _peerID)
{
    setAsking(_peerID, Asking::Nothing);
}

void BrcdChainCapability::setAsking(NodeID const& _peerID, Asking _a)
{
    auto itPeerStatus = m_peers.find(_peerID);
    if (itPeerStatus == m_peers.end()){
        LOG(m_logger) << "cant find peer " << _peerID;
        return;
    }


    auto& peerStatus = itPeerStatus->second;

    peerStatus.setAsking(_a);
    peerStatus.setLastAsk(std::chrono::system_clock::to_time_t(chrono::system_clock::now()));

    m_host->addNote(_peerID, "ask", ::toString(_a));
    m_host->addNote(_peerID, "sync",
            string(isCriticalSyncing(_peerID) ? "ONGOING" : "holding") + (needsSyncing(_peerID) ? " & needed" : ""));
}

bool BrcdChainCapability::isCriticalSyncing(NodeID const& _peerID) const
{
    auto itPeerStatus = m_peers.find(_peerID);
    if (itPeerStatus == m_peers.end())
        return false;

    auto const& peerStatus = itPeerStatus->second;

    auto const asking = peerStatus.asking();
    return asking == Asking::BlockHeaders || asking == Asking::State;
}

bool BrcdChainCapability::needsSyncing(NodeID const& _peerID) const
{
    if (m_host->isRude(_peerID, name()))
        return false;

    auto peerStatus = m_peers.find(_peerID);
    return (peerStatus != m_peers.end() && peerStatus->second.latestHash());
}

void BrcdChainCapability::disablePeer(NodeID const& _peerID, std::string const& _problem)
{
    m_host->disableCapability(_peerID, name(), _problem);
}

BrcdChainPeer const& BrcdChainCapability::peer(NodeID const& _peerID) const
{
    return const_cast<BrcdChainCapability*>(this)->peer(_peerID);
}

BrcdChainPeer& BrcdChainCapability::peer(NodeID const& _peerID)
{
    auto peer = m_peers.find(_peerID);
    if (peer == m_peers.end())
        BOOST_THROW_EXCEPTION(PeerDisconnected() << errinfo_nodeID(_peerID));

    return peer->second;
}

void dev::brc::BrcdChainCapability::sendNewBlock()
{
	std::vector<VerifiedSendData>const& _blocks = m_bq.getVerifiedBlocks();
	if(_blocks.empty())
		return;
	
    for (auto b: _blocks)
    {
        Timer _timer;
		BlockHeader _h = BlockHeader(b.m_block);
		h256 _hash = _h.hash();
        if(m_bq.inSended(_hash))
            continue;
		auto s = randomSelection(100, [&](BrcdChainPeer const& _peer){
			return !_peer.isBlockKnown(_hash);
								 });
		for(auto itr :  get<0>(s)){
            cwarn << "send chose " << itr ;
		}
		for(auto itr : get<1>(s)){
            cwarn << "send allow " << itr ;
		}

		for(NodeID const& peerID : get<0>(s))
		{

			RLPStream ts;
			m_host->prep(peerID, name(), ts, NewBlockPacket, 2)
				.appendRaw(b.m_block, 1)
				.append(b.m_totalDiff);

			auto itPeer = m_peers.find(peerID);
			if(itPeer != m_peers.end())
			{
				m_host->sealAndSend(peerID, ts);
				itPeer->second.markBlockAsKnown(_hash);
			}
		}
        cwarn << "send chose time" << _timer.elapsed()* 1000 << " ms";
		_timer.restart();

		for(NodeID const& peerID : get<1>(s))
		{
			RLPStream ts;
			m_host->prep(peerID, name(), ts, NewBlockHashesPacket,1);
			ts.appendList(2);
			ts.append(_hash);
			ts.append(_h.number());

			auto itPeer = m_peers.find(peerID);
			if(itPeer != m_peers.end())
			{
				m_host->sealAndSend(peerID, ts);
			}
		}
        cwarn << "send allow time" << _timer.elapsed() * 1000 << " ms";
        _timer.restart();
		m_latestBlockSent = _hash;
		m_bq.insertSendedHash(_hash);
        cwarn << "time:" << utcTimeMilliSec() << " send block " << (utcTimeMilliSec() - _h.timestamp()) << " height " << _h.number();
    }
	m_bq.clearVerifiedBlocks();
}

void BrcdChainCapability::debugMemery() {
    CMEM_LOG << "============== BrcdChainCapability start ==============";
    CMEM_LOG << "m_transactionsSent size: " << m_transactionsSent.size() * sizeof(h256);
    CMEM_LOG << "m_peers size: " << m_peers.size() ;
    m_sync->debugMemery();
    CMEM_LOG << "============== BrcdChainCapability end ==============";
}
