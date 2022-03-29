#ifndef SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_
#define SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_

#include "orch.h"
#include "portsorch.h"
#include "countercheckorch.h"

#define DEFAULT_POLLPERIOD 30
#define DEFAULT_THRESHOLD 5
#define TX_TIMER_NAME "MC_TX_ERROR_COUNTERS_POLL"

class TxPortErrStat{
    public:
        TxPortErrStat(std::string oid = "null"):m_oid(oid){};
    private:
        bool m_isOk{true};
        std::string m_oid{};
        u_int64_t m_errorCount{};

        friend class TxMonitorOrch;
};

typedef std::map<std::string, std::shared_ptr<TxPortErrStat>> IfToTxPortErrStat;

class TxMonitorOrch: public Orch
{
    public:
        static TxMonitorOrch& getInstance(TableConnector cfgTxTblConnector,
                                          TableConnector stateTxTblConnector,
                                          TableConnector cntrTblConnector,
                                          TableConnector ifToOidTblConnector);


        virtual void doTask(swss::SelectableTimer &timer);
        virtual void doTask(Consumer &consumer){}

    private:

        unique_ptr<swss::Table> m_cfgTxTbl;
        unique_ptr<swss::Table> m_stateTxTbl;
        unique_ptr<swss::Table> m_cntrTbl;
        unique_ptr<swss::Table> m_ifToOidTbl;
        uint32_t m_pollPeriod;
        uint32_t m_threshold;
        shared_ptr<IfToTxPortErrStat> m_ifToTxPortErrStat;
        void initPortsErrorStatisticsMap();
        void initCfgTxTbl();
        TxMonitorOrch(TableConnector cfgTxTblConnectorr,
                      TableConnector stateTxTblConnector,
                      TableConnector cntrTblConnector,
                      TableConnector ifToOidTblConnector);

        virtual ~TxMonitorOrch(void);
        std::string getOid(std::string interface);
        void poolTxErrorStatistics();
        bool getIsOkStatus(u_int64_t, u_int64_t);
        void updateStateDB(std::string, bool);
        u_int64_t getNewErrorCount(std::string currOid);


};

#endif /* SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_ */
