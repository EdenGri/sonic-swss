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
        bool m_isOk{true};
        std::string m_oid{};
        u_int64_t m_errorCount{};
        TxPortErrStat(std::string oid = "null"):m_oid(oid){}
        std::string getStatus(){
            if(m_isOk){
                return "ok";
            }
            return "not-ok";
        }
        std::string to_string(){
            return "status: " + getStatus() + "\n" +
                    "oid: " + m_oid +
                    "tx errors: " + std::to_string(m_errorCount) + "\n";
        }
};

typedef std::map<std::string, std::shared_ptr<TxPortErrStat>> IFtoTxPortErrStat;

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
        u_int32_t m_pollPeriod;
        u_int32_t m_threshold;
        IFtoTxPortErrStat createPortsErrorStatisticsMap();
        void initCfgTxTbl();
        TxMonitorOrch(TableConnector cfgTxTblConnectorr,
                      TableConnector stateTxTblConnector,
                      TableConnector cntrTblConnector,
                      TableConnector ifToOidTblConnector);

        virtual ~TxMonitorOrch(void);
        std::string getOid(std::string interface);
        void poolTxErrorStatistics(std::string interface,std::shared_ptr<TxPortErrStat> currTxStatistics);
        bool getIsOkStatus(u_int64_t, u_int64_t);
        void updateStateDB(std::string, std::string);
        u_int64_t getNewErrorCount(std::string currOid);


};

#endif /* SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_ */
