#ifndef SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_
#define SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_

#include "orch.h"
#include "portsorch.h"
#include "countercheckorch.h"

#define DEFAULT_POLLPERIOD 30
#define DEFAULT_THRESHOLD 5
class TxMonitorOrch: public Orch
{

    public:
        static TxMonitorOrch& getInstance(TableConnector configueTxTableConnector,
                                          TableConnector stateTxTableConnector,
                                          TableConnector countersTableConnector);

        void printPortsErrorStatistics();
        virtual void doTask(swss::SelectableTimer &timer);
        virtual void doTask(Consumer &consumer){}

    private:
        //todo: is ok change to function.. change to is ok to status string
        //to add m to fields
        class TxPortErrorStatistics{
            public:
                bool isOk{true};
                std::string m_oid{};
                u_int64_t errorCount{};
                TxPortErrorStatistics(std::string oid):m_oid(oid){}
                std::string getStatus(){
                    if(isOk){
                        return "ok";
                    }
                    return "not-ok";
                }
                std::string to_string(){
                    return "status: " + getStatus() + "\n" +
                            "oid: " + m_oid +
                            "tx errors: " + std::to_string(errorCount) + "\n";
                }
        };

        swss::Table m_configueTxTable;
        swss::Table m_stateTxTable;
        swss::Table m_countersTable;
        u_int32_t m_pollPeriod;
        u_int32_t m_threshold;
        std::map<std::string, TxPortErrorStatistics*> m_interfaceToPortErrorStatistics{};
        bool m_isPortsMapInitialized{};
        std::map<std::string, TxMonitorOrch::TxPortErrorStatistics*> createPortsErrorStatisticsMap();
        TxMonitorOrch(TableConnector configueTxTableConnectorr,
                      TableConnector stateTxTableConnector,
                      TableConnector countersTableConnector);
        virtual ~TxMonitorOrch(void);
        bool getIsOkStatus(u_int64_t, u_int64_t);
        void updateStateDB(std::string, std::string);
        u_int64_t getNewErrorCount(std::string currOid);


};

#endif /* SONIC_SWSS_ORCHAGENT_TXMONITORORCH_H_ */
