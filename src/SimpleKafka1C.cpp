﻿#include <boost/algorithm/string/split.hpp>          // boost::algorithm::split
#include <boost/algorithm/string/classification.hpp> // boost::is_any_of
#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "SimpleKafka1C.h"
#include "md5.h"

//================================== Utilites ==========================================

char *slice(char *s, size_t from, size_t to)
{
    size_t j = 0;
    for (size_t i = from; i <= to; ++i)
        s[j++] = s[i];
    s[j] = 0;
    return s;
};

const std::string currentDateTime()
{
    char buf[64];
#ifndef _WIN32
    struct timeval tv;

    gettimeofday(&tv, NULL);
    strftime(buf, sizeof(buf) - 1, "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
    sprintf(buf, "%s.%03d: ", buf, (int)(tv.tv_usec / 1000));
#else
    SYSTEMTIME lt = {0};
    GetLocalTime(&lt);
    // %Y-%m-%d %H:%M:%S.xxx:
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.%03d: ", lt.wYear, lt.wMonth,
            lt.wDay, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
#endif
    return std::string(buf);
}

unsigned long long getTimeStamp()
{
	time_t curtime = time(NULL);
	unsigned long long time = (unsigned long long)curtime;
	return time;
}
//================================== Events callback ===================================

void SimpleKafka1C::clEventCb::event_cb(RdKafka::Event &event)
{
    std::ofstream eventFile;
    if (!logsReportFileName.empty())
        eventFile.open(logsReportFileName, std::ios_base::app);

    switch (event.type())
    {
    case RdKafka::Event::EVENT_ERROR:
        if (event.fatal())
        {
            eventFile << currentDateTime() << "FATAL ";
        }
        eventFile << "ERROR (" << RdKafka::err2str(event.err())
                  << "): " << event.str() << std::endl;
        break;

    case RdKafka::Event::EVENT_STATS:
        eventFile << currentDateTime() << "\"STATS\": " << event.str() << std::endl;
        break;

    case RdKafka::Event::EVENT_LOG:
        char buf[512];
        sprintf(buf, "LOG-%i-%s: %s\n", event.severity(), event.fac().c_str(),
                event.str().c_str());
        eventFile << currentDateTime() << buf;
        break;

    case RdKafka::Event::EVENT_THROTTLE:
        eventFile << currentDateTime() << "THROTTLED: " << event.throttle_time() << "ms by "
                  << event.broker_name() << " id " << (int)event.broker_id()
                  << std::endl;
        break;

    default:
        eventFile << currentDateTime() << "EVENT " << event.type() << " ("
                  << RdKafka::err2str(event.err()) << "): " << event.str()
                  << std::endl;
        break;
    }

    eventFile.close();
}

void SimpleKafka1C::clDeliveryReportCb::dr_cb(RdKafka::Message &message)
{

    std::ofstream eventFile;
    std::string status_name;

    if (!logsReportFileName.empty())
        eventFile.open(logsReportFileName, std::ios_base::app);

    switch (message.status())
    {
    case RdKafka::Message::MSG_STATUS_NOT_PERSISTED:
        status_name = "NotPersisted";
        break;
    case RdKafka::Message::MSG_STATUS_POSSIBLY_PERSISTED:
        status_name = "PossiblyPersisted";
        break;
    case RdKafka::Message::MSG_STATUS_PERSISTED:
        delivered = true;
        status_name = "Persisted";
        break;
    default:
        status_name = "Unknown?";
        break;
    }

    eventFile << currentDateTime();

    if (message.key()->length())
    {
        eventFile << "Key:" << *(message.key()) << ", ";
    }
    else
    {
        eventFile << "Hash:" << md5(static_cast<char *>(message.payload())) << ", ";
    }

    eventFile << status_name << ":" << message.errstr() << ", "
              << "Size:" << message.len() << ", "
              << "Topic:" << message.topic_name() << ", "
              << "Offset:" << message.offset() << ", "
              << "Partition:" << message.partition() << ", "
              << "BrokerID:" << message.broker_id() << std::endl;

    eventFile.close();
}

void SimpleKafka1C::clRebalanceCb::rebalance_cb(RdKafka::KafkaConsumer *consumer,
		     RdKafka::ErrorCode err,
                     std::vector<RdKafka::TopicPartition*> &partitions) {
    if (err == RdKafka::ERR__ASSIGN_PARTITIONS || assignOffset > 0) {
      RdKafka::TopicPartition *part;

      for (unsigned int i = 0; i < partitions.size(); i++){
        if (partitions[i]->topic() == assignTopic && 
            partitions[i]->partition() == assignPartition){
            
            part = partitions[i];
            break;
        }           
      }  

      if (part)
         part->set_offset(assignOffset);
      consumer->assign(partitions);
    } else {
      consumer->unassign();
    }
}

//======================================================================================
std::string SimpleKafka1C::extensionName()
{
    return "SimpleKafka1C";
}

SimpleKafka1C::SimpleKafka1C()
{
    AddProperty(L"Version", L"ВерсияКомпоненты", [&]()
                {
        auto s = std::string(Version);
        return std::make_shared<variant_t>(std::move(s)); });

    AddMethod(L"SetLogsReportFileName", L"УстановитьФайлЛогирования", this, &SimpleKafka1C::setLogsReportFileName);

    AddMethod(L"SetParameter", L"УстановитьПараметр", this, &SimpleKafka1C::setParameter);
    AddMethod(L"InitializeProducer", L"ИнициализироватьПродюсера", this, &SimpleKafka1C::initProducer);
    AddMethod(L"Produce", L"ОтправитьСообщение", this, &SimpleKafka1C::produce,
              {{2, -1}, {3, std::string("")}, {4, std::string("")}});
    AddMethod(L"ProduceWithWaitResult", L"ОтправитьСообщениеСОжиданиемРезультата", this, &SimpleKafka1C::produceWithWaitResult,
              {{2, -1}, {3, std::string("")}, {4, std::string("")}});
    AddMethod(L"StopProducer", L"ОстановитьПродюсера", this, &SimpleKafka1C::stopProducer);

    AddMethod(L"InitializeConsumer", L"ИнициализироватьКонсьюмера", this, &SimpleKafka1C::initConsumer);
    AddMethod(L"Consume", L"Слушать", this, &SimpleKafka1C::consume);
    AddMethod(L"CommitOffset", L"ЗафиксироватьСмещение", this, &SimpleKafka1C::commitOffset, {{2, 0}});
    AddMethod(L"SetReadingPosition", L"УстановитьПозициюЧтения", this, &SimpleKafka1C::setReadingPosition, {{2, 0}});
    AddMethod(L"StopConsumer", L"ОстановитьКонсьюмера", this, &SimpleKafka1C::stopConsumer);
    AddMethod(L"SetWaitingTimeout", L"УстановитьТаймаутОжидания", this, &SimpleKafka1C::setWaitingTimeout);

    AddMethod(L"Message", L"Сообщить", this, &SimpleKafka1C::message);
    AddMethod(L"Sleep", L"Пауза", this, &SimpleKafka1C::sleep);

    waitMessageTimeout = 500;
}

void SimpleKafka1C::setLogsReportFileName(const variant_t &filename)
{
    logsReportFileName = std::get<std::string>(filename);
}

//================================== Settings ==========================================

void SimpleKafka1C::setParameter(const variant_t &key, const variant_t &value)
{
    settings.push_back({std::get<std::string>(key), std::get<std::string>(value)});
}

//================================== Producer ==========================================

bool SimpleKafka1C::initProducer(const variant_t &brokers)
{

    std::string msg_err = "";
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    if (conf->set("metadata.broker.list", std::get<std::string>(brokers), msg_err) != RdKafka::Conf::CONF_OK)
    {
        message(msg_err);
        return false;
    }

    for (int i = 0; i < settings.size(); i++)
    {
        if (conf->set(settings[i].Key, settings[i].Value, msg_err) != RdKafka::Conf::CONF_OK)
        {
            message(msg_err);
            return false;
        }
    }

    /* Set delivery report callback */
    conf->set("dr_cb", &cl_dr_cb, msg_err);

    hProducer = RdKafka::Producer::create(conf, msg_err);
    if (!hProducer)
    {
        message(msg_err);
        return false;
    }

    delete conf;
    return true;
}

variant_t SimpleKafka1C::produce(const variant_t &msg, const variant_t &topicName, const variant_t &partition, const variant_t &key, const variant_t &heads)
{

    if (!std::holds_alternative<std::string>(msg))
    {
        throw std::runtime_error(u8"Неподдерживаемый тип данных сообщения");
    }

    if (!std::holds_alternative<int>(partition))
    {
        throw std::runtime_error(u8"Неверный тип данных партиции");
    }

    std::string msg_err = "";
    std::string tTopicName = std::get<std::string>(topicName);
    auto currentPartition = std::get<int>(partition);
    std::vector<std::string> splitResult;
    boost::algorithm::split(splitResult, std::get<std::string>(heads), boost::is_any_of(";"));

    RdKafka::Headers *hdrs = RdKafka::Headers::create();

    for (std::string &s : splitResult)
    {
        std::vector<std::string> hKeyValue;
        boost::algorithm::split(hKeyValue, s, boost::is_any_of(","));
        if (hKeyValue.size() == 2)
            hdrs->add(hKeyValue[0], hKeyValue[1]);
    }

retry:
    RdKafka::ErrorCode resp = hProducer->produce(
        tTopicName,
        currentPartition == -1 ? RdKafka::Topic::PARTITION_UA : currentPartition,
        RdKafka::Producer::RK_MSG_COPY,
        const_cast<char *>(std::get<std::string>(msg).c_str()), std::get<std::string>(msg).size(),
        const_cast<char *>(std::get<std::string>(key).c_str()), std::get<std::string>(key).size(),
        0,
        hdrs,
        NULL);

    if (resp != RdKafka::ERR_NO_ERROR)
    {
        if (resp == RdKafka::ERR__QUEUE_FULL)
        {
            hProducer->poll(1000 /*block for max 1000ms*/);
            msg_err = "Достигнуто максимальное количество ожидающих сообщений: queue.buffering.max.message";
            message(msg_err);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            goto retry;
        }
        else if (resp == RdKafka::ERR_MSG_SIZE_TOO_LARGE)
        {
            msg_err = "Размер сообщения превышает установленный максимальный размер: message.max.bytes";
        }
        else if (resp == RdKafka::ERR__UNKNOWN_PARTITION)
        {
            msg_err = "Запрошенный partition неизвестен в кластере Kafka";
        }
        else if (resp == RdKafka::ERR__UNKNOWN_TOPIC)
        {
            msg_err = "Указанная тема не найдена в кластере Kafka";
        }
        delete hdrs;
    }

    hProducer->poll(0);

    if (!msg_err.empty())
    {
        message(msg_err);
        return false;
    }

    return true;
}

variant_t SimpleKafka1C::produceWithWaitResult(const variant_t &msg, const variant_t &topicName, const variant_t &partition, const variant_t &key, const variant_t &heads)
{
    delivered = false;
    auto timestart = getTimeStamp();
    produce(msg, topicName, partition, key, heads);

    while (delivered == false && (getTimeStamp() - timestart) < 20)
    {
        hProducer->poll(1000);
    }

    return delivered;
}

void SimpleKafka1C::stopProducer()
{

    hProducer->flush(10 * 1000 /* wait for max 10 seconds */);
    delete hProducer;
}

//================================== Consumer ==========================================

bool SimpleKafka1C::initConsumer(const variant_t &brokers, const variant_t &topic)
{

    std::string msg_err = "";
    std::ofstream logFile;
    std::vector<std::string> topics;

    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

    if (!logsReportFileName.empty())
        logFile.open(logsReportFileName, std::ios_base::app);

    if (logFile.is_open())
        logFile << currentDateTime() << " Librdkafka version: " << RdKafka::version_str()
                << " (" << RdKafka::version() << ")" << std::endl;

    boost::algorithm::split(topics, std::get<std::string>(topic), boost::is_any_of(","));

    // обязательный параметр
    if (conf->set("metadata.broker.list", std::get<std::string>(brokers), msg_err) != RdKafka::Conf::CONF_OK)
    {
        if (logFile.is_open())
            logFile << currentDateTime() << msg_err << std::endl;
        message(msg_err);
        return false;
    }

    // дополнительные параметры
    for (int i = 0; i < settings.size(); i++)
    {
        if (conf->set(settings[i].Key, settings[i].Value, msg_err) != RdKafka::Conf::CONF_OK)
        {
            if (logFile.is_open())
                logFile << currentDateTime() << msg_err << std::endl;
            message(msg_err);
            return false;
        }
    }

    // обратные вызовы для получения статистики и получения ошибок для дальнейшей обработки
    conf->set("event_cb", &cl_event_cb, msg_err);
    conf->set("rebalance_cb", &cl_rebalance_cb, msg_err);

    hConsumer = RdKafka::KafkaConsumer::create(conf, msg_err);
    if (!hConsumer)
    {
        if (logFile.is_open())
            logFile << currentDateTime() << msg_err << std::endl;
        message(msg_err);
        return false;
    }

    if (logFile.is_open())
        logFile << currentDateTime() << " Created consumer: " << hConsumer->name() << std::endl;

    RdKafka::ErrorCode resp = hConsumer->subscribe(topics);

    if (resp != RdKafka::ERR_NO_ERROR)
    {
        msg_err = RdKafka::err2str(resp);
        if (logFile.is_open())
            logFile << currentDateTime() << " Failed to start consumer: " << msg_err << std::endl;
        message(msg_err);
        return false;
    }

    // dump to log
    if (logFile.is_open())
    {
        std::list<std::string> *dump;
        dump = conf->dump();
        // logFile << currentDateTime() << " # Global config" << std::endl;

        for (std::list<std::string>::iterator it = dump->begin();
             it != dump->end();)
        {
            logFile << currentDateTime() << " setting: " << *it << " = ";
            it++;
            logFile << *it << std::endl;
            it++;
        }
        logFile.close();
    }

    delete conf;
    return true;
}

void SimpleKafka1C::setWaitingTimeout(const variant_t &timeout)
{
    waitMessageTimeout = std::get<int32_t>(timeout);
}

void SimpleKafka1C::setReadingPosition(const variant_t &topicName, const variant_t &offset, const variant_t &partition){
    cl_rebalance_cb.assignOffset = std::get<int32_t>(offset);
    cl_rebalance_cb.assignTopic = std::get<std::string>(topicName);
    cl_rebalance_cb.assignPartition = std::get<int32_t>(partition);
}

variant_t SimpleKafka1C::consume()
{

    std::ofstream logFile;
    std::stringstream s;
    std::string emptystr = "";

    boost::property_tree::ptree jsonObj;

    if (!logsReportFileName.empty())
        logFile.open(logsReportFileName, std::ios_base::app);

    try
    {
        RdKafka::Message *msg = hConsumer->consume(waitMessageTimeout);
        RdKafka::ErrorCode resultConsume = msg->err();

        if (resultConsume == RdKafka::ERR_NO_ERROR)
        {
            auto payload = static_cast<char *>(msg->payload());

            if (msg->key())
            {
                jsonObj.put("key", *msg->key());
            }

            RdKafka::MessageTimestamp ts = msg->timestamp();

            jsonObj.put("partition", msg->partition());
            jsonObj.put("offset", (long)msg->offset());
            jsonObj.put("message", std::string(slice(payload, 0, msg->len())));
            jsonObj.put("topic", msg->topic_name());
            jsonObj.put("timestamp", ts.timestamp);

            delete msg;
        }
        else
        {
            if (logFile.is_open() && resultConsume != RdKafka::ERR__TIMED_OUT)
            {
                logFile << currentDateTime() << " error: " << msg->errstr() << std::endl;
                logFile.close();
            }
            delete msg;
            return emptystr;
        }
    }
    catch (const std::exception &e)
    {
        if (logFile.is_open())
        {
            logFile << currentDateTime() << " error: " << e.what() << std::endl;
            logFile.close();
        }

        return emptystr;
    }

    boost::property_tree::write_json(s, jsonObj, true);
    return s.str();
}

bool SimpleKafka1C::commitOffset(const variant_t &topicName, const variant_t &offset, const variant_t &partition)
{
    std::vector<RdKafka::TopicPartition *> offsets;

    std::int32_t tOffset = std::get<std::int32_t>(offset); // todo: variant_t -> 64bit integer
    std::int32_t tPartition = std::get<std::int32_t>(partition);
    std::string tTopicName = std::get<std::string>(topicName);

    RdKafka::TopicPartition *ptr = RdKafka::TopicPartition::create(tTopicName, tPartition, tOffset);
    offsets.push_back(ptr);

    if (hConsumer->offsets_store(offsets) != RdKafka::ERR_NO_ERROR)
    {
        return false;
    }
    return true;
}

void SimpleKafka1C::stopConsumer()
{
    hConsumer->close();
    delete hConsumer;
    RdKafka::wait_destroyed(10 * 1000);
}

//================================== Utilites ==========================================

void SimpleKafka1C::message(const variant_t &msg)
{
    std::visit(overloaded{[&](const std::string &v)
                          { AddError(ADDIN_E_INFO, extensionName(), v, false); },
                          [&](const int32_t &v)
                          {
                              AddError(ADDIN_E_INFO, extensionName(), std::to_string(static_cast<int>(v)), false);
                          },
                          [&](const double &v)
                          { AddError(ADDIN_E_INFO, extensionName(), std::to_string(v), false); },
                          [&](const bool &v)
                          {
                              AddError(ADDIN_E_INFO, extensionName(), std::string(v ? u8"Истина" : u8"Ложь"), false);
                          },
                          [&](const std::tm &v)
                          {
                              std::ostringstream oss;
                              oss.imbue(std::locale("ru_RU.utf8"));
                              oss << std::put_time(&v, "%c");
                              AddError(ADDIN_E_INFO, extensionName(), oss.str(), false);
                          },
                          [&](const std::vector<char> &v) {},
                          [&](const std::monostate &) {}},
               msg);
}

void SimpleKafka1C::sleep(const variant_t &delay)
{
    using namespace std;
    this_thread::sleep_for(chrono::seconds(get<int32_t>(delay)));
}