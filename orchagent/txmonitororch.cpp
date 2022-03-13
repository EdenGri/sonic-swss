#include "txmonitororch.h"
extern PortsOrch* gPortsOrch;

TxMonitorOrch& TxMonitorOrch::getInstance(TableConnector cfgTxTblConnector,
                                          TableConnector stateTxTblConnector,
                                          TableConnector cntrTblConnector,
                                          TableConnector ifToOidTblConnector)
{
    SWSS_LOG_ENTER();
    static TxMonitorOrch* txMonitorOrch = new TxMonitorOrch(cfgTxTblConnector,
                                                            stateTxTblConnector,
                                                            cntrTblConnector,
                                                            ifToOidTblConnector);
    return *txMonitorOrch;
}


// TODO: change the m_pollPeriod and m_threshold from default values to the configurable values
TxMonitorOrch::TxMonitorOrch(TableConnector cfgTxTblConnector,
                             TableConnector stateTxTblConnector,
                             TableConnector cntrTblConnector,
                             TableConnector ifToOidTblConnector):
    Orch(cfgTxTblConnector.first,
         cfgTxTblConnector.second),
    m_pollPeriod{DEFAULT_POLLPERIOD},
    m_threshold{DEFAULT_THRESHOLD}
{

    SWSS_LOG_ENTER();
    m_cfgTxTbl = unique_ptr<Table>(new Table(cfgTxTblConnector.first,
                                             cfgTxTblConnector.second));
    m_stateTxTbl = unique_ptr<Table>(new Table(stateTxTblConnector.first,
                                               stateTxTblConnector.second));
    m_cntrTbl = unique_ptr<Table>(new Table(cntrTblConnector.first,
                                            cntrTblConnector.second));
    m_ifToOidTbl = unique_ptr<Table>(new Table(ifToOidTblConnector.first,
                                               ifToOidTblConnector.second));
    initCfgTxTbl();
    auto interv = timespec { .tv_sec = m_pollPeriod, .tv_nsec = 0 };
    auto timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(timer, this, TX_TIMER_NAME);
    Orch::addExecutor(executor);
    SWSS_LOG_NOTICE("TxMonitorOrch initialized with the tables: %s, %s, %s, %s\n",
                    cfgTxTblConnector.second.c_str(),
                    stateTxTblConnector.second.c_str(),
                    cntrTblConnector.second.c_str(),
                    ifToOidTblConnector.second.c_str());


    timer->start();
}

void TxMonitorOrch::initCfgTxTbl(){
    /*

     table name: "CFG_PORT_TX_ERROR_TABLE"
     "polling_period"
     <period value>
     "threshold"
     <threshold value>

     */

    vector<FieldValueTuple> fvs;
    fvs.emplace_back("polling_period", std::to_string(m_pollPeriod));
    fvs.emplace_back("threshold",std::to_string(m_threshold));
    m_cfgTxTbl->set("",fvs);
}

IFtoTxPortErrStat TxMonitorOrch::createPortsErrorStatisticsMap(){

    SWSS_LOG_ENTER();
    IFtoTxPortErrStat ifToTxPortErrStat{};
    map<string, Port> &ifToPort =  gPortsOrch->getAllPorts();

        for (auto &entry : ifToPort)
        {
            string interface = entry.first;
            Port p = entry.second;

            SWSS_LOG_NOTICE("interface name: %s", interface.c_str());
            if (p.m_type != Port::PHY)
            {
                continue;
            }
            std::string oidStr;
            if(m_ifToOidTbl->hget("",interface,oidStr)){
                SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
                ifToTxPortErrStat[interface]=make_unique<TxPortErrStat>(oidStr);
                updateStateDB(oidStr.c_str(),"ok");
            }else{
                SWSS_LOG_NOTICE("cant get oid for interface %s", interface.c_str());
                ifToTxPortErrStat[interface]=make_unique<TxPortErrStat>();
            }

        }
    SWSS_LOG_NOTICE("create txMonitorOrch map");
    SWSS_LOG_NOTICE("The number of interfaces in txMonitorOrch map: %lu",
                    ifToTxPortErrStat.size());
    return ifToTxPortErrStat;

    }

TxMonitorOrch::~TxMonitorOrch(void){
    SWSS_LOG_ENTER();
}

void TxMonitorOrch::updateStateDB(std::string currOid,std::string status){
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fvs;
    fvs.emplace_back("status", status);
    m_stateTxTbl->set(currOid, fvs);
    SWSS_LOG_NOTICE("update stateDB txErrorTable at key oid: %s with the new status  %s", currOid.c_str(), status.c_str());
}

std::string TxMonitorOrch::getOid(std::string interface){

    std::string oidStr;
    if(m_ifToOidTbl->hget("",interface,oidStr)){
        SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
        return oidStr;
    }else{
        SWSS_LOG_NOTICE("cant get oid for interfae %s", interface.c_str());
        return "null";
    }
}


u_int64_t TxMonitorOrch::getNewErrorCount(std::string currOid){
    std::string newErrorCountStr;
    if(m_cntrTbl->hget(currOid, "SAI_PORT_STAT_IF_OUT_ERRORS",newErrorCountStr)){
        SWSS_LOG_NOTICE("new tx counter: %s\n",newErrorCountStr.c_str());
        return stoul(newErrorCountStr);
    }
    return 0;
}



bool TxMonitorOrch::getIsOkStatus(u_int64_t newErrorCount, u_int64_t currErrorCount){
    SWSS_LOG_ENTER();
    return (currErrorCount-newErrorCount) < this->m_threshold;
}

void TxMonitorOrch::poolTxErrorStatistics(std::string interface,std::shared_ptr<TxPortErrStat> currTxStatistics){
    SWSS_LOG_NOTICE("check statistics and update for interface: %s.\n",interface.c_str());
    std::string currOid = currTxStatistics->m_oid;
    if(currOid=="null"){
        currTxStatistics->m_oid =getOid(interface);
        if(currTxStatistics->m_oid == "null"){
            return;
        }
        updateStateDB(currTxStatistics->m_oid,"ok");
    }

    u_int64_t currErrorCount = currTxStatistics->m_errorCount;
    SWSS_LOG_NOTICE("last tx counter: %s\n",std::to_string(currErrorCount).c_str());
    u_int64_t newErrorCount = getNewErrorCount(currOid);
    if(newErrorCount && newErrorCount){
       bool newIsOk = getIsOkStatus(newErrorCount, currErrorCount);
       if(newIsOk != currTxStatistics->m_isOk){
           currTxStatistics->m_isOk = newIsOk;
           updateStateDB(currOid, currTxStatistics->getStatus());

       }
    }
    currTxStatistics->m_errorCount = newErrorCount;
}

void TxMonitorOrch::doTask(SelectableTimer &timer){
    SWSS_LOG_ENTER();
    if(!gPortsOrch->allPortsReady()){
        SWSS_LOG_NOTICE("Ports are not ready yet");
        return;
    }
    static IFtoTxPortErrStat ifToTxPortErrStat = createPortsErrorStatisticsMap();
    for(const auto& entry : ifToTxPortErrStat){

        std::string interface = entry.first;
        std::shared_ptr<TxPortErrStat> currTxStatistics = entry.second;
        poolTxErrorStatistics(interface,currTxStatistics);

    }
}


//TODO: take care in case configure change in runtime
