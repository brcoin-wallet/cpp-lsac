#pragma once


#include <libdevcore/Common.h>
#include <libbrccore/Common.h>
#include <array>
#include <boost/optional.hpp>

namespace dev
{
namespace brc
{

struct BRCSchedule
{
    BRCSchedule(): tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}) {}
    BRCSchedule(bool _efcd, bool _hdc, unsigned const& _txCreateGas): exceptionalFailedCodeDeposit(_efcd), haveDelegateCall(_hdc), tierStepGas(std::array<unsigned, 8>{{0, 2, 3, 5, 8, 10, 20, 0}}), txCreateGas(_txCreateGas) {}
    // BRCSchedule(BRCSchedule const& _schedule, AdditionalEIPs const& _eips);
    unsigned accountVersion = 0;
    bool exceptionalFailedCodeDeposit = true;
    bool haveDelegateCall = true;
    bool eip150Mode = false;
    bool eip158Mode = false;
    bool eip1283Mode = false;
    bool eip2200Mode = false;
    bool haveBitwiseShifting = false;
    bool haveRevert = false;
    bool haveReturnData = false;
    bool haveStaticCall = false;
    bool haveCreate2 = false;
    bool haveExtcodehash = false;
    bool haveSelfbalance = false;
    bool haveChainID = false;
    std::array<unsigned, 8> tierStepGas;
    unsigned expGas = 10;
    unsigned expByteGas = 10;
    unsigned sha3Gas = 30;
    unsigned sha3WordGas = 6;
    unsigned sloadGas = 50;
    unsigned sstoreSetGas = 20000;
    unsigned sstoreResetGas = 5000;
    unsigned sstoreUnchangedGas = 200;
    unsigned sstoreRefundGas = 15000;
    unsigned sstoreRefundNonzeroGas = 4800;
    unsigned jumpdestGas = 1;
    unsigned logGas = 375;
    unsigned logDataGas = 8;
    unsigned logTopicGas = 375;
    unsigned createGas = 32000;
    unsigned callGas = 40;
    unsigned precompileStaticCallGas = 700;
    unsigned callSelfGas = 40;
    unsigned callStipend = 2300;
    unsigned callValueTransferGas = 9000;
    unsigned callNewAccountGas = 25000;
    unsigned suicideRefundGas = 24000;
    unsigned memoryGas = 3;
    unsigned quadCoeffDiv = 512;
    unsigned createDataGas = 200;
    unsigned txGas = 21000;
    unsigned txCreateGas = 53000;
    unsigned txDataZeroGas = 4;
    unsigned txDataNonZeroGas = 68;
    unsigned copyGas = 3;

    unsigned extcodesizeGas = 20;
    unsigned extcodecopyGas = 20;
    unsigned extcodehashGas = 400;
    unsigned balanceGas = 20;
    unsigned suicideGas = 0;
    unsigned selfdestructGas = 0;
    unsigned blockhashGas = 20;
    unsigned maxCodeSize = unsigned(-1);

    boost::optional<u256> blockRewardOverwrite;

    bool staticCallDepthLimit() const { return !eip150Mode; }
    bool suicideChargesNewAccountGas() const { return eip150Mode; }
    bool emptinessIsNonexistence() const { return eip158Mode; }
    bool zeroValueTransferChargesNewAccountGas() const { return !eip158Mode; }
    bool sstoreNetGasMetering() const { return eip1283Mode || eip2200Mode; }
    bool sstoreThrowsIfGasBelowCallStipend() const { return eip2200Mode; }
};

static const BRCSchedule DefaultSchedule = BRCSchedule();
static const BRCSchedule FrontierSchedule = BRCSchedule(false, false, 21000);
static const BRCSchedule HomesteadSchedule = BRCSchedule(true, true, 53000);

static const BRCSchedule EIP150Schedule = []
{
    BRCSchedule schedule = HomesteadSchedule;
    schedule.eip150Mode = true;
    schedule.extcodesizeGas = 700;
    schedule.extcodecopyGas = 700;
    schedule.balanceGas = 400;
    schedule.sloadGas = 200;
    schedule.callGas = 700;
    schedule.callSelfGas = 700;
    schedule.selfdestructGas = 5000;
    return schedule;
}();

static const BRCSchedule EIP158Schedule = []
{
    BRCSchedule schedule = EIP150Schedule;
    schedule.expByteGas = 50;
    schedule.eip158Mode = true;
    schedule.maxCodeSize = 0x6000;
    return schedule;
}();

static const BRCSchedule ByzantiumSchedule = []
{
    BRCSchedule schedule = EIP158Schedule;
    schedule.haveRevert = true;
    schedule.haveReturnData = true;
    schedule.haveStaticCall = true;
    schedule.blockRewardOverwrite = {3 * brcer};
    return schedule;
}();

static const BRCSchedule EWASMSchedule = []
{
    BRCSchedule schedule = ByzantiumSchedule;
    schedule.maxCodeSize = std::numeric_limits<unsigned>::max();
    return schedule;
}();

static const BRCSchedule ConstantinopleSchedule = []
{
    BRCSchedule schedule = ByzantiumSchedule;
    schedule.haveCreate2 = true;
    schedule.haveBitwiseShifting = true;
    schedule.haveExtcodehash = true;
    schedule.eip1283Mode = true;
    schedule.blockRewardOverwrite = {2 * brcer};
    return schedule;
}();

static const BRCSchedule ConstantinopleFixSchedule = [] {
    BRCSchedule schedule = ConstantinopleSchedule;
    schedule.eip1283Mode = false;
    return schedule;
}();

static const BRCSchedule IstanbulSchedule = [] {
    BRCSchedule schedule = ConstantinopleFixSchedule;
    schedule.txDataNonZeroGas = 16;
    schedule.sloadGas = 800;
    schedule.balanceGas = 700;
    schedule.extcodehashGas = 700;
    schedule.haveChainID = true;
    schedule.haveSelfbalance = true;
    schedule.eip2200Mode = true;
    schedule.sstoreUnchangedGas = 800;
    return schedule;
}();

static const BRCSchedule& MuirGlacierSchedule = IstanbulSchedule;

static const BRCSchedule BerlinSchedule = [] {
    BRCSchedule schedule = MuirGlacierSchedule;
    return schedule;
}();

static const BRCSchedule ExperimentalSchedule = [] {
    BRCSchedule schedule = ConstantinopleSchedule;
    schedule.blockhashGas = 800;
    return schedule;
}();

static const BRCSchedule newBifurcationBvmSchedule = [] {
    BRCSchedule schedule = BerlinSchedule;
    schedule.accountVersion = 1;
    schedule.blockhashGas = 800;
    return schedule;
}();

inline BRCSchedule const& latestScheduleForAccountVersion(u256 const& _version)
{
    if (_version == 0)
        return IstanbulSchedule;
    else if (_version == ExperimentalSchedule.accountVersion)
        return ExperimentalSchedule;
    else
    {
        // This should not happen, as all existing accounts
        // are created either with version 0 or with one of fork's versions
        assert(false);
        return DefaultSchedule;
    }
}


}
}
