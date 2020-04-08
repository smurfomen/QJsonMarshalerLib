#ifndef ARRAYKEEPERS_H
#define ARRAYKEEPERS_H
#include <qserializerlib_global.h>

#include <propertykeeper.h>
#include <vector>
#include <QMetaProperty>
#include <QMetaType>
#include <QVariant>
#include <serializerexception.h>

/// \brief возвращает один из Meta хранителей, подходящих для переданного свойства связанного объекта, обернутый в базовый интерфейс хранителя
PropertyKeeper * getMetaKeeper(QObject *obj, QMetaProperty prop);

/// \brief возвраащет коллекцию Meta хранителей для объекта обернутых в базовый интерфейс хранителя
std::vector<PropertyKeeper*> getMetaKeepers(QObject * obj);

/// \brief абстрактный класс для Meta хранителей
class MetaPropertyKeeper : public PropertyKeeper
{
public:
    MetaPropertyKeeper(QObject * obj, QMetaProperty prop)
    {
        this->linkedObj = obj;
        this->prop = prop;
    }
protected:

    /// \brief накачивает переданный объект переданным JSONом
    void fillObjectFromJson(QObject * qo, QJsonValue json)
    {
        if(!json.isObject())
            throw QSException(JsonObjectExpected);

        /// перебор всех ключей у переданного JSON и всех хранителей у объекта, если нашли хранителя с подходящим ключом - отдаем значение по ключу в найденный хранитель
        /// дальше он сам разберется что ему с ним делать, в зависимости от типа хранителя за прослойкой интерфейса
        QJsonObject jsonObject = json.toObject();
        QStringList keys = jsonObject.keys();
        std::vector<PropertyKeeper*> metaKeepers = getMetaKeepers(qo);
        for(QString & key : keys)
        {
            for(int i = 0; i < metaKeepers.size(); i++)
            {
                PropertyKeeper * keeper = metaKeepers.at(i);
                QString keeperKey = keeper->getValue().first;
                if(key == keeperKey)
                {
                    keeper->setValue(jsonObject.value(key));
                    metaKeepers.erase(metaKeepers.begin()+i);
                }
            }
        }
    }

    /// \brief выкачивает JSON из переданного объекта
    QJsonObject getJsonFromObject(QObject * qo)
    {
        /// взять коллекцию простых хранителей, хранящих в себе элементарные данные и взять у каждого хранителя его ключ и JSON значение
        /// составить из этих значений объект.
        /// внутри коллекции хранителей может быть любой хранитель (как элементарный так и хранитель вложенного объекта)
        /// сбор информации будет продолжаться до тех пор, пока не упрется в последние элементарные хранители самых глубоковложенных объектов
        /// и так же JSON значения (QJsonValue) будут возвращаться и сливаться в объекты до тех пор, пока не вернутся в корень для возврата вернут сформированного JSON объекта
        QJsonObject json;
        std::vector<PropertyKeeper*> keepers = getMetaKeepers(qo);
        for(PropertyKeeper * keeper : keepers)
        {
            std::pair<QString, QJsonValue> keeperValue = keeper->getValue();
            json.insert(keeperValue.first, keeperValue.second);
        }
        return json;
    }

    QObject * linkedObj;
    QMetaProperty prop;
};




/// \brief хранитель обычного поля (не массива) QMetaProperty у указанного QObject
class QMetaSimpleKeeper : public MetaPropertyKeeper
{
public:
    QMetaSimpleKeeper(QObject * obj, QMetaProperty prop): MetaPropertyKeeper(obj, prop){ }
    /// \brief вернуть пару из ключа и JSON значения из указанной QMetaProperty связанного объекта
    std::pair<QString, QJsonValue> getValue() override
    {
        QJsonValue result = QJsonValue::fromVariant(prop.read(linkedObj));
        return std::make_pair(QString(prop.name()), result);
    }

    /// \brief задать новое значение для связанной с хранимой QMetaProperty поля связанного объекта из JSON
    void setValue(QJsonValue val) override
    {
        prop.write(linkedObj, QVariant(val));
    }
};




/// \brief хранитель массивов типа А для поля QMetaProperty у указанного QObject
template<typename A>
class QMetaArrayKeeper : public MetaPropertyKeeper
{
public:
    QMetaArrayKeeper(QObject * obj, QMetaProperty prop): MetaPropertyKeeper(obj, prop) { }
    /// \brief возвращает пару из имени хранимого поля и cформированного в массив QJsonValue
    std::pair<QString, QJsonValue> getValue() override
    {
        QVariant property = prop.read(linkedObj);
        std::vector<A> values = property.value<std::vector<A>>();

        QJsonArray result;
        for(auto val : values)
        {
            result.push_back(QJsonValue::fromVariant(QVariant(val)));
        }

        return std::make_pair(QString(prop.name()), QJsonValue(result));
    }

    /// \brief изменение значения поля из JSON по переданному QJsonValue
    void setValue(QJsonValue json) override
    {
        /// переводим значение в массив и наполняем вектор типа А, с которым был создан хранитель значениями из JSON массива
        /// в конце записываем в хранимое свойство связанного объекта этот вектор
        if(!json.isArray())
            throw QSException(JsonArrayExpected);

        QJsonArray arr = json.toArray();
        std::vector<A> v;
        for(auto item :arr)
        {
            QVariant itemVariant(item);
            v.push_back(itemVariant.value<A>());
        }
        prop.write(linkedObj, QVariant::fromValue(v));
    }
};




/// \brief хранитель полей класса, типы которых унаследованны от QObject
class QMetaObjectKeeper : public MetaPropertyKeeper
{
    /// такой тип хранителя предназначен для хранения вложенных объектов, т.е. объектов, унаследованных от QObject
    /// вместо изменения свойства напрямую, он разбирает хранимый объект на элементарные составляющие или такие же хранители объектов
    /// и служит "маршрутизатором" JSON значений внутрь хранимых объектов и обратно.
    /// QMetaObjectKeeper, фактически, является воплощением QMetaObject для связанного с ним вложенного объекта.
public:
    QMetaObjectKeeper(QObject * obj, QMetaProperty prop): MetaPropertyKeeper(obj, prop) { }
    /// \brief возвращает пару из имени хранимого поля и упакованного в QJsonValue объекта JSON
    std::pair<QString, QJsonValue> getValue() override
    {
        QJsonObject result = getJsonFromObject(linkedObj);
        return std::make_pair(prop.name(),QJsonValue(result));
    }

    /// \brief изменение значения хранимого объекта по переданному QJsonValue, в который упакован объект
    void setValue(QJsonValue json) override
    {
        fillObjectFromJson(linkedObj, json);
    }
};




/// \brief хранитель массивов типа QObject или базового от него
class QMetaObjectArrayKeeper : public MetaPropertyKeeper
{
public:
    QMetaObjectArrayKeeper(QObject * obj, QMetaProperty prop) : MetaPropertyKeeper(obj, prop){ }
    /// \brief возвращает пару из имени хранимого поля и упакованного в QJsonValue массива объектов
    std::pair<QString, QJsonValue> getValue() override
    {
        QJsonArray result;
        QVariant property = prop.read(linkedObj);
        std::vector<QObject*> * objects = static_cast<std::vector<QObject*>*>(property.data());

        if(objects != nullptr && (objects->size() == 0 || qobject_cast<QObject*>(objects->at(0)) != nullptr))
        {
            for(QObject * qo : *objects)
                result.push_back(getJsonFromObject(qo));
        } else throw QSException(InvalidQObject);

        return std::make_pair(QString(prop.name()), QJsonValue(result));
    }

    /// \brief изменение значения хранимого массива объектов по переданному JSON массиву обернутому в QJsonValue
    void setValue(QJsonValue json) override
    {
        if(!json.isArray())
            throw QSException(JsonArrayExpected);

        QJsonArray jsonArray = json.toArray();
        QVariant property = prop.read(linkedObj);
        std::vector<QObject*> * objects = static_cast<std::vector<QObject*>*>(property.data());

        if(objects != nullptr && (objects->size() == 0 || qobject_cast<QObject*>(objects->at(0)) != nullptr))
        {
            for(int i = 0; i < jsonArray.size() && i < objects->size(); i ++)
                fillObjectFromJson(objects->at(i),jsonArray.at(i));
        } else throw QSException(InvalidQObject);
    }
};




#endif // ARRAYKEEPERS_H