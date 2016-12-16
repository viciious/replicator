#ifndef REPLICATOR_SERIALIZABLE_H
#define REPLICATOR_SERIALIZABLE_H

#include <string>
#include <map>
#include <sstream>
#include <boost/any.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/vector.hpp>

namespace replicator {

class SerializableValue {
private:
	friend class boost::serialization::access;

	std::string type_id;

	template<class Archive>
	void serialize(Archive &ar, const unsigned int file_version){
		ar & type_id & second;
	}

	void fromAny (const boost::any &v)
	{
		if (v.type() == typeid(std::string)) {
			type_id = "string";
			second = boost::any_cast<std::string>(v);
		} else {
			std::ostringstream s;

			if (v.type() == typeid(int)) {
				type_id = "int";
				s << boost::any_cast<int>(v);
			}
			else if (v.type() == typeid(unsigned int)) {
				type_id = "uint";
				s << boost::any_cast<unsigned int>(v);
			}
			else if (v.type() == typeid(double)) {
				type_id = "double";
				s << boost::any_cast<double>(v);
			}
			else if (v.type() == typeid(unsigned long long)) {
				type_id = "ull";
				s << boost::any_cast<unsigned long long>(v);
			}
			else if (v.type() == typeid(float)) {
				type_id = "float";
				s << boost::any_cast<float>(v);
			}
			else if (v.type() == typeid(char)) {
				type_id = "int";
				s << int(boost::any_cast<char>(v));
			}
			else if (v.type() == typeid(unsigned char)) {
				type_id = "uint";
				s << unsigned(boost::any_cast<unsigned char>(v));
			}
			else if (v.type() == typeid(short)) {
				type_id = "int";
				s << int(boost::any_cast<short>(v));
			}
			else if (v.type() == typeid(unsigned short)) {
				type_id = "uint";
				s << unsigned(boost::any_cast<unsigned short>(v));
			}
			else if (v.type() == typeid(void)) {
				type_id = "null";
			}
			else  {
				type_id = "long";
				s << boost::any_cast<long>(v);
			}
			second = s.str();
		}
	}

public:
	std::string second;

	SerializableValue ()
	{
	}

	SerializableValue (const boost::any &v)
	{
		fromAny(v);
	}

	SerializableValue & operator = (const boost::any &v)
	{
		fromAny(v);
		return *this;
	}

	boost::any operator *() const {
		if (type_id == "string") {
			return boost::any(second);
		}

		std::istringstream s(second);

		if (type_id == "uint") {
			unsigned int val;
			s >> val;
			return boost::any(val);
		}
		if (type_id == "int") {
			int val;
			s >> val;
			return boost::any(val);
		}
		if (type_id == "double") {
			double val;
			s >> val;
			return boost::any(val);
		}
		if (type_id == "float") {
			float val;
			s >> val;
			return boost::any(val);
		}
		if (type_id == "ull") {
			unsigned long long val;
			s >> val;
			return boost::any(val);
		}
		if (type_id == "null") {
			return boost::any();
		}

		long val;
		s >> val;
		return boost::any(val);
	}

	const std::string & value_string() const {
		return second;
	}
};

typedef std::vector<SerializableValue> SerializableRow;

class SerializableBinlogEvent
{
private:
	friend class boost::serialization::access;

	template<class Archive>
	void serialize(Archive &ar, const unsigned int file_version){
		ar & binlog_name & binlog_pos & seconds_behind_master & unix_timestamp & database & table & event & row;
	}

public:
	std::string binlog_name;
	unsigned long binlog_pos;
	unsigned long seconds_behind_master;
	unsigned long unix_timestamp;
	std::string database;
	std::string table;
	std::string event;
	SerializableRow row;
};

} // replicator

#endif // REPLICATOR_SERIALIZABLE_H
