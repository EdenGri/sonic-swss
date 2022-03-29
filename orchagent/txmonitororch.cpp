#include "txmonitororch.h"
#include "converter.h"

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
     table name: "CFG_PORT_TX_ERROR_TABLE" "values"
     "polling_period"
     <period value>
     "threshold"
     <threshold value>
     */

    vector<FieldValueTuple> fvs;
    fvs.emplace_back("polling_period", std::to_string(m_pollPeriod));
    fvs.emplace_back("threshold",std::to_string(m_threshold));
    m_cfgTxTbl->set("values",fvs);
}

void TxMonitorOrch::initPortsErrorStatisticsMap(){

    SWSS_LOG_ENTER();
    map<string, Port> &ifToPort =  gPortsOrch->getAllPorts();
    m_ifToTxPortErrStat = make_shared<IfToTxPortErrStat>();
    std::string oidStr;
    for (auto &entry : ifToPort)
    {
        string interface = entry.first;
        Port p = entry.second;

        SWSS_LOG_NOTICE("interface name: %s", interface.c_str());
        if(p.m_type != Port::PHY)
        {
            continue;
        }

        if(m_ifToOidTbl->hget("",interface,oidStr))
        {
            SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
            SWSS_LOG_NOTICE("eden new mark to delete");
            m_ifToTxPortErrStat->emplace(interface, make_shared<TxPortErrStat>(oidStr));
            SWSS_LOG_NOTICE("eden after insertion ");
            updateStateDB(oidStr.c_str(),true);
        }
        else
        {
            //todo:warning
            SWSS_LOG_NOTICE("cant get oid for interface %s", interface.c_str());
            m_ifToTxPortErrStat->emplace(interface,make_unique<TxPortErrStat>());
        }

    }
    SWSS_LOG_NOTICE("create txMonitorOrch map");
}

TxMonitorOrch::~TxMonitorOrch(void){
    SWSS_LOG_ENTER();
}

void TxMonitorOrch::updateStateDB(std::string currOid,bool is_ok){
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fvs;
    std::string status;
    if(is_ok)
    {
        status= "ok";
    }
    else
    {
        status= "not-ok";
    }
    fvs.emplace_back("status", status);
    m_stateTxTbl->set(currOid, fvs);
    SWSS_LOG_NOTICE("update stateDB txErrorTable at key oid: %s with the new status  %s", currOid.c_str(), status.c_str());
}

std::string TxMonitorOrch::getOid(std::string interface){

    std::string oidStr;
    if(m_ifToOidTbl->hget("",interface,oidStr))
    {
        SWSS_LOG_NOTICE("oid: %s", oidStr.c_str());
        return oidStr;
    }
    else
    {
        SWSS_LOG_NOTICE("cant get oid for interfae %s", interface.c_str());
        return std::string{};
    }
}


bool TxMonitorOrch::getNewErrorCount(std::string currOid, u_int64_t& newErrorCount){

    std::string newErrorCountStr;
    m_cntrTbl->hget(currOid, "SAI_PORT_STAT_IF_OUT_ERRORS",newErrorCountStr);
    try
    {
        newErrorCount = to_uint<uint32_t>(newErrorCountStr);
        return true;
    }
    catch(const std::exception& err)
    {
        SWSS_LOG_ERROR("Get tx error counter faild");
        return false;
    }

}



bool TxMonitorOrch::getIsOkStatus(u_int64_t newErrorCount, u_int64_t currErrorCount){
    SWSS_LOG_ENTER();
    return (currErrorCount-newErrorCount) < m_threshold;
}

void TxMonitorOrch::poolTxErrorStatistics(){
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("eden before loop");
    if(!m_ifToTxPortErrStat){
        SWSS_LOG_NOTICE("eden map is null");

    }

    /*IfToTxPortErrStat map = *m_ifToTxPortErrStat;
    SWSS_LOG_NOTICE("eden map.size is: %u", map.size());
    IfToTxPortErrStat::iterator it;
    for(it = map.begin(); it != map.end(); it++){
        SWSS_LOG_NOTICE("eden in loop");
    }
    */
    SWSS_LOG_NOTICE("eden map.size is: %lu", (*m_ifToTxPortErrStat).size());
    for( auto& entry : *m_ifToTxPortErrStat){
        SWSS_LOG_NOTICE("eden 5");
        std::string interface = entry.first;

        SWSS_LOG_NOTICE("Check statistics and update for interface [%s]",interface.c_str());
        //todo share ptr?
        std::shared_ptr<TxPortErrStat> currTxStatistics = entry.second;
        std::string currOid = currTxStatistics->m_oid;
        if(currOid==string{})
        {
            currTxStatistics->m_oid =getOid(interface);
            if(currTxStatistics->m_oid == string{})
            {
                //todo warnning
                return;
            }
            updateStateDB(currTxStatistics->m_oid,true);
        }

        u_int64_t currErrorCount = currTxStatistics->m_errorCount;
        SWSS_LOG_NOTICE("last tx counter: %s\n",std::to_string(currErrorCount).c_str());
        u_int64_t newErrorCount = currErrorCount;
        if(getNewErrorCount(currOid, newErrorCount))
        {
           bool newIsOk = getIsOkStatus(newErrorCount, currErrorCount);
           if(newIsOk != currTxStatistics->m_isOk)
           {
               currTxStatistics->m_isOk = newIsOk;
               updateStateDB(currOid, newIsOk);

           }
        }
        currTxStatistics->m_errorCount = newErrorCount;
    }

}

void TxMonitorOrch::doTask(SelectableTimer &timer){
    SWSS_LOG_NOTICE("eden do task 11");
    SWSS_LOG_ENTER();
    if(!m_ifToTxPortErrStat)
    {
        SWSS_LOG_NOTICE("eden do task 22");
        initPortsErrorStatisticsMap();
    }
    SWSS_LOG_NOTICE("eden do task 33");
    poolTxErrorStatistics();
}


//TODO: take care in case configure change in runtime
