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
    m_timer = new SelectableTimer(interv);
    auto executor = new ExecutableTimer(m_timer, this, TX_TIMER_NAME);
    Orch::addExecutor(executor);
    SWSS_LOG_NOTICE("TxMonitorOrch initialized with the tables: %s, %s, %s, %s\n",
                    cfgTxTblConnector.second.c_str(),
                    stateTxTblConnector.second.c_str(),
                    cntrTblConnector.second.c_str(),
                    ifToOidTblConnector.second.c_str());


    m_timer->start();
}

void TxMonitorOrch::initCfgTxTbl()
{
    /*
     table name: "CFG_PORT_TX_ERROR_TABLE" "Config"
     "polling_period"
     <period value>
     "threshold"
     <threshold value>
     */
    SWSS_LOG_ENTER();
    vector<FieldValueTuple> fvs;
    fvs.emplace_back("polling_period", std::to_string(m_pollPeriod));
    fvs.emplace_back("threshold",std::to_string(m_threshold));
    m_cfgTxTbl->set("Config",fvs);
    SWSS_LOG_NOTICE("Init CFG_PORT_TX_ERROR_TABLE with default values");
}

void TxMonitorOrch::initPortsErrorStatisticsMap()
{

    SWSS_LOG_ENTER();
    map<string, Port> &ifToPort =  gPortsOrch->getAllPorts();
    m_ifToTxPortErrStat = new IfToTxPortErrStat();
    std::string oidStr;
    for (auto &entry : ifToPort)
    {
        string interface = entry.first;
        Port p = entry.second;

        SWSS_LOG_DEBUG("interface name: %s", interface.c_str());
        if(p.m_type != Port::PHY)
        {
            continue;
        }

        if(m_ifToOidTbl->hget("",interface,oidStr))
        {
            SWSS_LOG_DEBUG("oid: %s", oidStr.c_str());
            m_ifToTxPortErrStat->emplace(interface, TxPortErrStat(oidStr));
            updateStateDB(oidStr.c_str(),true);
        }
        else
        {
            SWSS_LOG_WARN("cant get oid for interface %s", interface.c_str());
            m_ifToTxPortErrStat->emplace(interface,TxPortErrStat());
        }

    }
    SWSS_LOG_NOTICE("create txMonitorOrch map");
}

TxMonitorOrch::~TxMonitorOrch(void)
{
    SWSS_LOG_ENTER();
    delete m_ifToTxPortErrStat;
}

void TxMonitorOrch::updateStateDB(std::string currOid,bool is_ok)
{
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

std::string TxMonitorOrch::getOid(std::string interface)
{

    std::string oidStr;
    if(m_ifToOidTbl->hget("",interface,oidStr))
    {
        SWSS_LOG_DEBUG("oid: %s", oidStr.c_str());
        return oidStr;
    }
    else
    {
        SWSS_LOG_WARN("cant get oid for interfae %s", interface.c_str());
        return std::string{};
    }
}


bool TxMonitorOrch::getNewErrorCount(std::string currOid, u_int64_t& newErrorCount)
{

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



bool TxMonitorOrch::getIsOkStatus(u_int64_t newErrorCount, u_int64_t currErrorCount)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_DEBUG("New error count: [%lu]", newErrorCount);
    SWSS_LOG_DEBUG("Curr error count: [%lu]", currErrorCount);
    SWSS_LOG_DEBUG("Curr threshold: [%u]", m_threshold);

    return (newErrorCount-currErrorCount) < m_threshold;
}

void TxMonitorOrch::poolTxErrorStatistics()
{
    SWSS_LOG_ENTER();
    for( auto& entry : *m_ifToTxPortErrStat){
        std::string interface = entry.first;
        SWSS_LOG_DEBUG("Check statistics and update for interface [%s]",interface.c_str());
        TxPortErrStat& currTxStatistics = entry.second;
        std::string currOid = currTxStatistics.m_oid;
        if(currOid==string{})
        {
            currTxStatistics.m_oid =getOid(interface);
            if(currTxStatistics.m_oid == string{})
            {
                SWSS_LOG_WARN("Faild get oid for %s interface", interface.c_str());
                return;
            }
            updateStateDB(currTxStatistics.m_oid,true);
        }

        u_int64_t currErrorCount = currTxStatistics.m_errorCount;
        SWSS_LOG_NOTICE("last tx counter: %s\n",std::to_string(currErrorCount).c_str());
        u_int64_t newErrorCount = currErrorCount;
        if(getNewErrorCount(currOid, newErrorCount))
        {
           bool newIsOk = getIsOkStatus(newErrorCount, currErrorCount);
           if(newIsOk != currTxStatistics.m_isOk)
           {
               currTxStatistics.m_isOk = newIsOk;
               updateStateDB(currOid, newIsOk);

           }
        }
        currTxStatistics.m_errorCount = newErrorCount;
    }

}

void TxMonitorOrch::doTask(SelectableTimer &timer)
{
    SWSS_LOG_ENTER();
    if(!m_ifToTxPortErrStat)
    {
        initPortsErrorStatisticsMap();
    }
    poolTxErrorStatistics();
}

void TxMonitorOrch::setTimer()
{
    auto interval = timespec { .tv_sec = m_pollPeriod, .tv_nsec = 0 };
    m_timer->setInterval(interval);
    m_timer->reset();
}

void TxMonitorOrch::handlePeriodUpdate(std::string newValue)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("update period update");

    try
    {
        uint32_t newPeriod = to_uint<uint32_t>(newValue);
        if(m_pollPeriod != newPeriod){
            m_pollPeriod = newPeriod;
            SWSS_LOG_DEBUG("m_pollPeriod set [%u]", m_pollPeriod);
            setTimer();
        }
    }
    catch(const std::exception& e)
    {
        SWSS_LOG_ERROR("Period is a number but got %s", newValue.c_str());
        SWSS_LOG_ERROR("runtime Eerror %s", e.what());
    }
}

void TxMonitorOrch::handleThresholdUpdate(std::string newValue)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Handle threshold update");
    try
    {
        m_threshold = to_uint<uint32_t>(newValue);
        SWSS_LOG_DEBUG("m_threshold set [%u]", m_threshold);
    }
    catch(const std::exception& e)
    {
        SWSS_LOG_ERROR("Threshold is a number but got %s", newValue.c_str());
        SWSS_LOG_ERROR("runtime Eerror %s", e.what());
    }

}
void TxMonitorOrch::handleConfigUpdate(std::vector<FieldValueTuple> fvs)
{
    SWSS_LOG_NOTICE("Handle configure update");
    for(FieldValueTuple fv : fvs)
    {
        std::string field = fvField(fv);
        std::string value = fvValue(fv);

        if(field == "polling_period")
        {
           handlePeriodUpdate(value);
        }
        else if(field == "threshold")
        {
            handleThresholdUpdate(value);
        }
        else
        {
            SWSS_LOG_ERROR("Unexpected field");
        }
    }

}

void TxMonitorOrch::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();
    try
    {
        auto it = consumer.m_toSync.begin();
        while(it!=consumer.m_toSync.end()){

            KeyOpFieldsValuesTuple currEvent= it->second;
            std::string key = kfvKey(currEvent);
            std::string op = kfvOp(currEvent);

            if(key == "Config" && op == SET_COMMAND)
            {
                handleConfigUpdate(kfvFieldsValues(currEvent));
            }
            else
            {
                SWSS_LOG_ERROR("Unexpected operation or key");
            }
            consumer.m_toSync.erase(it++);
        }
    }
    catch(const std::exception& e)
    {
        SWSS_LOG_ERROR("Runtime error: %s", e.what());
    }
}



