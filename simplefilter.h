#ifndef SIMPLE_FILTER_H
#define SIMPLE_FILTER_H

#include <algorithm>
#include <map>
#include <string>
#include <Slave.h>
#include "serializable.h"

namespace replicator
{

class SimplePredicate
{
	public:
		SimplePredicate() : column(0), negate(false) {}

		SimplePredicate(unsigned column, bool negate, const std::vector<int64_t> & values) : 
			column(column), negate(negate), values(values)
		{
			std::sort(this->values.begin(), this->values.end());
		}

		unsigned column;
		bool negate;
		std::vector<int64_t> values;
};

class SimpleFilter
{
	public:
		void AddPredicate(const std::string &db, const std::string &tbl, const SimplePredicate &pred)
		{
			predicates[std::pair<std::string, std::string>(db,tbl)] = pred;
		}

		template<typename T>
		bool PassEvent(const std::string &db, const std::string &tbl, const T &row)
		{
			auto it = predicates.find(std::pair<std::string, std::string>(db,tbl));
			if (it != predicates.end()) {
				SimplePredicate &pred = it->second;

				const boost::any &a = row[pred.column].second;
				int64_t ival = 0;

				if (a.type() == typeid(int)) {
					ival = boost::any_cast<int>(a);
				} else if (a.type() == typeid(unsigned int)) {
					ival = boost::any_cast<unsigned int>(a);
				} else if (a.type() == typeid(long long)) {
					ival = boost::any_cast<long long>(a);
				} else if (a.type() == typeid(unsigned long long)) {
					ival = boost::any_cast<unsigned long long>(a);
				} else  if (a.type() == typeid(long)) {
					ival = boost::any_cast<long>(a);
				} else  if (a.type() == typeid(unsigned long)) {
					ival = boost::any_cast<unsigned long>(a);
				} else  if (a.type() == typeid(short)) {
					ival = boost::any_cast<short>(a);
				} else  if (a.type() == typeid(unsigned short)) {
					ival = boost::any_cast<unsigned short>(a);
				} else  if (a.type() == typeid(char)) {
					ival = boost::any_cast<char>(a);
				} else  if (a.type() == typeid(unsigned char)) {
					ival = boost::any_cast<unsigned char>(a);
				} else  if (a.type() == typeid(std::string)) {
					ival = atoi(boost::any_cast<std::string>(a).c_str());
				} else {
					return pred.negate;
				}

				bool res = std::binary_search(pred.values.begin(), pred.values.end(), ival) ^ pred.negate;
				return res;
			}

			return true;
		}

	private:
		std::map< std::pair<std::string,std::string>, SimplePredicate > predicates;
};

}

#endif
