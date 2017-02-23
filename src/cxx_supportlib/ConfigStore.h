/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */
#ifndef _PASSENGER_CONFIG_STORE_H_
#define _PASSENGER_CONFIG_STORE_H_

#include <string>
#include <vector>

#include <oxt/backtrace.hpp>
#include <jsoncpp/json.h>

#include <DataStructures/StringKeyTable.h>
#include <Logging.h>
#include <Exceptions.h>
#include <StaticString.h>
#include <Utils/JsonUtils.h>
#include <Utils/FastStringStream.h>

namespace Passenger {

using namespace std;


/**
 * A configuration definition and storage system that plays well with JSON.
 * Features and properties:
 *
 *  - Configuration keys are typed according to a schema.
 *  - Type validation.
 *  - Default values, which may either be static or dynamically calculated.
 *  - Only stores configuration keys defined in the schema.
 *  - Partial updates.
 *  - Keeping track of which values are explicitly supplied, and which ones are not.
 *
 * ### Defining the schema
 *
 * Start using ConfigStore by defining the keys in the schema.
 *
 *     ConfigStore store;
 *
 *     // A required string key.
 *     store.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
 *
 *     // An optional integer key without default value.
 *     store.registerKey("bar", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
 *
 *     // An optional integer key, with default value 123.
 *     store.registerKey("baz", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL,
 *         ConfigStore::staticDefaultValue(123));
 *
 * See ConfigStore::Type for all supported types.
 *
 * ### Putting data in the store
 *
 * You can populate the store using the `update()` method. The method also
 * performs validation against the schema. The update only succeeds if validation
 * passes.
 *
 *     vector<ConfigStore::Error> errors;
 *     Json::Value updates1;
 *
 *     // Validation fails: 'foo' is missing
 *     store.update(updates1, errors);
 *     // => return value: false
 *     //    errors: 1 item ("'foo' is required")
 *
 *     store.get("foo").isNull();
 *     // => true, because the update failed
 *
 *     errors.clear();
 *     updates1["foo"] = "strval";
 *     store.update(updates1, errors);
 *     // => return value: true
 *     //    errors: empty
 *
 *     store.get("foo").asString();
 *     // => "strval", because the update succeeded
 *
 * #### Updating data
 *
 * Any further calls to `update` only update the keys that you actually pass
 * to the method, not the keys that you don't pass:
 *
 *     // Assuming we are using the store from 'Putting data in the store'.
 *     Json::Value updates2;
 *
 *     updates2["bar"] = 123.45;
 *     store.update(updates2, errors); // => true
 *     store.get("foo").asString();    // => still "strval"
 *     store.get("bar").asDouble();    // => 123.45
 *
 * ### Unregistered keys are ignored
 *
 * `update` ignores keys that aren't registered in the schema:
 *
 *     // Assuming we are using the store that went through
 *     // 'Putting data in the store' and 'Updating data'.
 *     Json::Value updates3;
 *
 *     updates3["unknown"] = true;
 *     store.update(updates3, errors); // => true
 *     store.get("foo").asString();    // => still "strval"
 *     store.get("bar").asDouble();    // => still 123.45
 *     store.get("unknown").isNull();  // => true
 *
 * ## Deleting data
 *
 * You can delete data by calling `update` with null values on the keys
 * you want to delete.
 *
 *     // Assuming we are using the store that went through
 *     // 'Putting data in the store' and 'Updating data'.
 *     Json::Value deletionSpec;
 *
 *     deletionSpec["bar"] = Json::Value(Json::nullValue);
 *     store.update(deletionSpec, errors);
 *     // => return value: true
 *     //    errors: empty
 *
 *     store.get("bar").isNull();   // => true
 *
 * ## Fetching data
 *
 * Use the `get` method or the `[]` operator to fetch data from the store.
 * They both return a Json::Value.
 *
 *     // Assuming we are using the store that went through
 *     // 'Putting data in the store' and 'Updating data'.
 *     store.get("foo").asString();    // => "strval"
 *     store["foo"].asString();        // => same
 *
 * If the key is not defined then they either return the default value
 * as defined in the schema, or (if no default value is defined) a
 * null Json::Value.
 *
 *     // Assuming we are using the store that went through
 *     // 'Putting data in the store' and 'Updating data'.
 *     store.get("baz").asInt();       // => 123
 *     store.get("unknown").isNull();  // => true
 *
 * ### Dumping all data
 *
 * You can fetch an overview of all data in the store using `dump()`.
 * This will return a Json::Value in the following format:
 *
 *     // Assuming we are using the store that went through
 *     // 'Putting data in the store' and 'Updating data'.
 *
 *     {
 *       "foo": {
 *         "user_value": "strval",
 *         "effective_value": "strval",
 *         "type": "string",
 *         "required": true
 *       },
 *       "bar": {
 *         "user_value": 123.45,
 *         "effective_value": 123.45,
 *         "type": "float"
 *       },
 *       "baz": {
 *         "user_value": null,
 *         "default_value": 123,
 *         "effective_value": 123,
 *         "type": "integer"
 *       }
 *     }
 *
 * Description of the members:
 *
 *  - `user_value`: the value as explicitly set in the store. If null
 *    then it means that the value isn't set.
 *  - `default_value`: the default value as defined in the schema. May
 *    be absent.
 *  - `effective_value`: the effective value, i.e. the value that
 *    `get()` will return.
 *  - `type`: the schema definition's type. Could be one of "string",
 *    "integer", "unsigned integer", "float" or "boolean".
 *  - `required`: whether this key is required.
 */
class ConfigStore {
public:
	enum Type {
		STRING_TYPE,
		INTEGER_TYPE,
		UNSIGNED_INTEGER_TYPE,
		FLOAT_TYPE,
		BOOLEAN_TYPE,
		UNKNOWN_TYPE
	};

	enum Options {
		OPTIONAL = 0,
		REQUIRED = 1
	};

	struct Error {
		string key;
		string message;

		Error() { }

		Error(const string &_key, const string &_message)
			: key(_key),
			  message(_message)
			{ }

		string getFullMessage() const {
			if (key.empty()) {
				return message;
			} else {
				return "'" + key + "' " + message;
			}
		}

		bool operator<(const Error &other) const {
			return getFullMessage() < other.getFullMessage();
		}
	};

	typedef boost::function<Json::Value ()> ValueGetter;

private:
	struct Entry {
		Type type;
		Options options;
		Json::Value userValue;
		ValueGetter defaultValueGetter;

		Entry()
			: type(UNKNOWN_TYPE),
			  options(OPTIONAL),
			  userValue(Json::nullValue)
			{ }

		Entry(Type _type, Options _options, const Json::Value &_userValue,
			const ValueGetter &_defaultValueGetter)
			: type(_type),
			  options(_options),
			  userValue(_userValue),
			  defaultValueGetter(_defaultValueGetter)
			{ }

		Json::Value getEffectiveValue() const {
			return ConfigStore::getEffectiveValue(userValue, defaultValueGetter);
		}

		void dumpProperties(Json::Value &doc) const {
			doc["type"] = ConfigStore::getTypeString(type).data();
			if (options & REQUIRED) {
				doc["required"] = true;
			}
		}
	};

	StringKeyTable<Entry> entries;


	static Json::Value getEffectiveValue(const Json::Value &userValue,
		const ValueGetter &defaultValueGetter)
	{
		if (userValue.isNull()) {
			if (defaultValueGetter) {
				return defaultValueGetter();
			} else {
				return Json::Value(Json::nullValue);
			}
		} else {
			return userValue;
		}
	}

	static StaticString getTypeString(Type type) {
		switch (type) {
		case STRING_TYPE:
			return P_STATIC_STRING("string");
		case INTEGER_TYPE:
			return P_STATIC_STRING("integer");
		case UNSIGNED_INTEGER_TYPE:
			return P_STATIC_STRING("unsigned integer");
		case FLOAT_TYPE:
			return P_STATIC_STRING("float");
		case BOOLEAN_TYPE:
			return P_STATIC_STRING("boolean");
		default:
			return P_STATIC_STRING("unknown");
		}
	}

	void validateRequiredKeysExist(const Json::Value &dump, vector<Error> &errors) const {
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			if (entry.options & REQUIRED) {
				const Json::Value value = dump[it.getKey()];
				if (value["effective_value"].isNull()) {
					errors.push_back(Error(it.getKey(), "is required"));
				}
			}
			it.next();
		}
	}

	void validateExistantKeys(const Json::Value &dump, vector<Error> &errors) const {
		Error error;
		Json::Value::const_iterator it, end = dump.end();

		for (it = dump.begin(); it != end; it++) {
			const Entry *entry;
			HashedStaticString key = it.memberName();
			const Json::Value &value = *it;

			if (!entries.lookup(key, &entry)) {
				continue;
			}

			if (!validateExistantKey(key, value["effective_value"], *entry, error)) {
				error.key = key;
				errors.push_back(error);
			}
		}
	}

	bool validateExistantKey(const HashedStaticString &key, const Json::Value &value,
		const Entry &entry, Error &error) const
	{
		if (value.isNull()) {
			// Already checked by the first loop inside validate().
			return true;
		}

		switch (entry.type) {
		case STRING_TYPE:
			if (value.isConvertibleTo(Json::stringValue)) {
				return true;
			} else {
				error.message = "must be a string";
				return false;
			}
		case INTEGER_TYPE:
			if (value.isConvertibleTo(Json::intValue)) {
				return true;
			} else {
				error.message = "must be an integer";
				return false;
			}
		case UNSIGNED_INTEGER_TYPE:
			if (value.isConvertibleTo(Json::intValue)) {
				if (value.isConvertibleTo(Json::uintValue)) {
					return true;
				} else {
					error.message = "must be greater than 0";
					return false;
				}
			} else {
				error.message = "must be an integer";
				return false;
			}
		case FLOAT_TYPE:
			if (value.isConvertibleTo(Json::realValue)) {
				return true;
			} else {
				error.message = "must be a number";
				return false;
			}
		case BOOLEAN_TYPE:
			if (value.isConvertibleTo(Json::booleanValue)) {
				return true;
			} else {
				error.message = "must be a boolean";
				return false;
			}
		default:
			P_BUG("Unknown type " + toString((int) entry.type));
			return false;
		};
	}

	static Json::Value returnJsonValue(const Json::Value &v) {
		return v;
	}

public:
	/**
	 * Register a new schema entry.
	 */
	void registerKey(const HashedStaticString &key, Type type, int options,
		const ValueGetter &defaultValueGetter = ValueGetter())
	{
		if (options & REQUIRED && defaultValueGetter) {
			throw ArgumentException("A key cannot be required and have a default value at the same time");
		}
		Entry entry(type, (Options) options,
			Json::Value(Json::nullValue),
			defaultValueGetter);
		entries.insert(key, entry);
	}

	/**
	 * Returns the effective value of the given configuration key.
	 * That is: either the user-supplied value, or the default value,
	 * or null (whichever is first applicable).
	 *
	 * Note that `key` *must* be NULL-terminated!
	 */
	Json::Value get(const HashedStaticString &key) const {
		const Entry *entry;

		if (entries.lookup(key, &entry)) {
			return entry->getEffectiveValue();
		} else {
			return Json::Value(Json::nullValue);
		}
	}

	Json::Value operator[](const HashedStaticString &key) const {
		return get(key);
	}

	/**
	 * Given a JSON document containing configuration updates, returns
	 * a JSON document that describes how the new configuration would
	 * look like (when the updates are merged with the existing configuration),
	 * and whether it passes validation, without actually updating the
	 * stored configuration.
	 *
	 * You can use the `forceApplyUpdatePreview` method to apply the result, but
	 * be sure to do that only if validation passes.
	 *
	 * If validation fails then any validation errors will be added to `errors`.
	 *
	 * Any keys in `updates` that are not registered are omitted from the result.
	 * Any keys not in `updates` do not affect existing values stored in the store.
	 *
	 * The format returned by this method is the same as that of `dump()`.
	 */
	Json::Value previewUpdate(const Json::Value &updates, vector<Error> &errors) const {
		if (!updates.isNull() && !updates.isObject()) {
			errors.push_back(Error(string(), "The JSON document must be an object"));
			return dump();
		}

		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);
		while (*it != NULL) {
			const Entry &entry = it.getValue();
			Json::Value subdoc(Json::objectValue);

			if (updates.isMember(it.getKey())) {
				subdoc["user_value"] = updates[it.getKey()];
			} else {
				subdoc["user_value"] = entry.userValue;
			}
			if (entry.defaultValueGetter) {
				subdoc["default_value"] = entry.defaultValueGetter();
			}
			subdoc["effective_value"] = getEffectiveValue(subdoc["user_value"],
				entry.defaultValueGetter);
			entry.dumpProperties(subdoc);

			result[it.getKey()] = subdoc;
			it.next();
		}

		validateRequiredKeysExist(result, errors);
		validateExistantKeys(result, errors);

		return result;
	}

	/**
	 * Applies the result of `updatePreview()` without performing any
	 * validation. Be sure to only call this if you've verified that
	 * `updatePreview()` passes validation, otherwise you will end up
	 * with invalid data in the store.
	 */
	void forceApplyUpdatePreview(const Json::Value &preview) {
		StringKeyTable<Entry>::Iterator it(entries);
		while (*it != NULL) {
			Entry &entry = it.getValue();
			const Json::Value &subdoc =
				const_cast<const Json::Value &>(preview)[it.getKey()];
			entry.userValue = subdoc["user_value"];
			it.next();
		}
	}

	/**
	 * Attempts to merge the given configuration updates into this store.
	 * Only succeeds if the merged result passes validation. Any
	 * validation errors are stored in `errors`.
	 * Returns whether the update succeeded.
	 *
	 * Any keys in `updates` that are not registered will not participate in the update.
	 * Any keys not in `updates` do not affect existing values stored in the store.
	 */
	bool update(const Json::Value &updates, vector<Error> &errors) {
		Json::Value preview = previewUpdate(updates, errors);
		if (errors.empty()) {
			forceApplyUpdatePreview(preview);
			return true;
		} else {
			return false;
		}
	}

	/**
	 * Dumps the current store's configuration keys and values in a format
	 * that displays user-supplied and effective values, as well as
	 * other useful information. See the overview section "Dumping all data"
	 * to learn about the format.
	 */
	Json::Value dump() const {
		Json::Value result(Json::objectValue);
		StringKeyTable<Entry>::ConstIterator it(entries);

		while (*it != NULL) {
			const Entry &entry = it.getValue();
			Json::Value subdoc(Json::objectValue);

			subdoc["user_value"] = entry.userValue;
			if (entry.defaultValueGetter) {
				subdoc["default_value"] = entry.defaultValueGetter();
			}
			subdoc["effective_value"] = entry.getEffectiveValue();
			entry.dumpProperties(subdoc);

			result[it.getKey()] = subdoc;
			it.next();
		}

		return result;
	}


	/**
	 * Helper method for defining a static default value registering a key.
	 * A static default value is one that is not dynamically computed.
	 *
	 *     config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL,
	 *         ConfigStore::staticDefaultValue("string"));
	 *
	 *     config["foo"].asString(); // => "string"
	 */
	static ValueGetter staticDefaultValue(const Json::Value &v) {
		return boost::bind(returnJsonValue, v);
	}
};


inline string
toString(const vector<ConfigStore::Error> &errors) {
	FastStringStream<> stream;
	vector<ConfigStore::Error>::const_iterator it, end = errors.end();

	for (it = errors.begin(); it != end; it++) {
		if (it != errors.begin()) {
			stream << "; ";
		}
		stream << it->getFullMessage();
	}
	return string(stream.data(), stream.size());
}


} // namespace Passenger

#endif /* _PASSENGER_CONFIG_STORE_H_ */
