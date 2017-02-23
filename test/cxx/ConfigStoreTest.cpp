#include <TestSupport.h>
#include <ConfigStore.h>
#include <algorithm>

using namespace Passenger;
using namespace std;

namespace tut {
	struct ConfigStoreTest {
		Json::Value doc;
		ConfigStore config;
		vector<ConfigStore::Error> errors;
	};

	DEFINE_TEST_GROUP(ConfigStoreTest);

	/*********** Test validation ***********/

	TEST_METHOD(1) {
		set_test_name("Validating an empty schema against an empty update set succeeds");
		config.previewUpdate(doc, errors);
		ensure(errors.empty());
	}

	TEST_METHOD(2) {
		set_test_name("Validating an empty schema against a non-empty update set succeeds");
		doc["foo"] = "bar";
		config.previewUpdate(doc, errors);
		ensure(errors.empty());
	}

	TEST_METHOD(3) {
		set_test_name("Validating a non-object update set");
		doc = Json::Value("hello");
		config.previewUpdate(doc, errors);
		ensure_equals(errors.size(), 1u);
		ensure_equals(errors[0].getFullMessage(), "The JSON document must be an object");
	}

	TEST_METHOD(5) {
		set_test_name("Validating required but non-existant keys");

		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("bar", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);

		doc["bar"] = Json::Value(Json::nullValue);

		config.previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 2u);
		ensure_equals(errors[0].getFullMessage(), "'bar' is required");
		ensure_equals(errors[1].getFullMessage(), "'foo' is required");
	}

	TEST_METHOD(6) {
		set_test_name("Validating required and existant keys with the right value types");

		config.registerKey("string_string", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("string_integer", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("string_real", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("string_boolean", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_integer", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_real", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_boolean", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_signed", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_unsigned", ConfigStore::UNSIGNED_INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("float_float", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("float_integer", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_boolean", ConfigStore::BOOLEAN_TYPE, ConfigStore::REQUIRED);
		config.registerKey("boolean_integer", ConfigStore::BOOLEAN_TYPE, ConfigStore::REQUIRED);
		config.registerKey("boolean_real", ConfigStore::BOOLEAN_TYPE, ConfigStore::REQUIRED);

		doc["string_string"] = "string";
		doc["string_integer"] = 123;
		doc["string_real"] = 123.45;
		doc["string_boolean"] = true;
		doc["integer_integer"] = 123;
		doc["integer_real"] = 123.45;
		doc["integer_boolean"] = true;
		doc["integer_signed"] = -123;
		doc["integer_unsigned"] = 123;
		doc["float_float"] = 123.45;
		doc["float_integer"] = 123;
		doc["boolean_boolean"] = true;
		doc["boolean_integer"] = 123;
		doc["boolean_real"] = 123.45;

		config.previewUpdate(doc, errors);
		ensure_equals(errors.size(), 0u);
	}

	TEST_METHOD(7) {
		set_test_name("Validating required and non-existant keys with the wrong value types");

		config.registerKey("integer_string", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("integer_unsigned", ConfigStore::UNSIGNED_INTEGER_TYPE, ConfigStore::REQUIRED);
		config.registerKey("float_string", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_string", ConfigStore::BOOLEAN_TYPE, ConfigStore::REQUIRED);

		doc["integer_string"] = "string";
		doc["integer_unsigned"] = -123;
		doc["float_string"] = "string";
		doc["boolean_string"] = "string";

		config.previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 4u);
		ensure_equals(errors[0].getFullMessage(), "'boolean_string' must be a boolean");
		ensure_equals(errors[1].getFullMessage(), "'float_string' must be a number");
		ensure_equals(errors[2].getFullMessage(), "'integer_string' must be an integer");
		ensure_equals(errors[3].getFullMessage(), "'integer_unsigned' must be greater than 0");
	}

	TEST_METHOD(10) {
		set_test_name("Validating optional and non-existant keys");
		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL);
		config.previewUpdate(doc, errors);
		ensure_equals(errors.size(), 0u);
	}

	TEST_METHOD(11) {
		set_test_name("Validating optional and existant keys with the right value types");

		config.registerKey("string_string", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("string_integer", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("string_real", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("string_boolean", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_integer", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_real", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_boolean", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_signed", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_unsigned", ConfigStore::UNSIGNED_INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("float_float", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("float_integer", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_boolean", ConfigStore::BOOLEAN_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_integer", ConfigStore::BOOLEAN_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_real", ConfigStore::BOOLEAN_TYPE, ConfigStore::OPTIONAL);

		doc["string_string"] = "string";
		doc["string_integer"] = 123;
		doc["string_real"] = 123.45;
		doc["string_boolean"] = true;
		doc["integer_integer"] = 123;
		doc["integer_real"] = 123.45;
		doc["integer_boolean"] = true;
		doc["integer_signed"] = -123;
		doc["integer_unsigned"] = 123;
		doc["float_float"] = 123.45;
		doc["float_integer"] = 123;
		doc["boolean_boolean"] = true;
		doc["boolean_integer"] = 123;
		doc["boolean_real"] = 123.45;

		config.previewUpdate(doc, errors);
		ensure_equals(errors.size(), 0u);
	}

	TEST_METHOD(12) {
		set_test_name("Validating optional and non-existant keys with the wrong value types");

		config.registerKey("integer_string", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("integer_unsigned", ConfigStore::UNSIGNED_INTEGER_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("float_string", ConfigStore::FLOAT_TYPE, ConfigStore::OPTIONAL);
		config.registerKey("boolean_string", ConfigStore::BOOLEAN_TYPE, ConfigStore::OPTIONAL);

		doc["integer_string"] = "string";
		doc["integer_unsigned"] = -123;
		doc["float_string"] = "string";
		doc["boolean_string"] = "string";

		config.previewUpdate(doc, errors);
		std::sort(errors.begin(), errors.end());
		ensure_equals(errors.size(), 4u);
		ensure_equals(errors[0].getFullMessage(), "'boolean_string' must be a boolean");
		ensure_equals(errors[1].getFullMessage(), "'float_string' must be a number");
		ensure_equals(errors[2].getFullMessage(), "'integer_string' must be an integer");
		ensure_equals(errors[3].getFullMessage(), "'integer_unsigned' must be greater than 0");
	}


	/*********** Test other stuff ***********/

	TEST_METHOD(20) {
		set_test_name("previewUpdate()");

		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("bar", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);

		doc["foo"] = "string";
		doc["baz"] = true;

		Json::Value preview = config.previewUpdate(doc, errors);
		ensure_equals("1 error", errors.size(), 1u);
		ensure_equals(errors[0].getFullMessage(), "'bar' is required");
		ensure("foo exists", preview.isMember("foo"));
		ensure("bar exists", preview.isMember("bar"));
		ensure("baz does not exists", !preview.isMember("baz"));
		ensure_equals("foo is a string", preview["foo"]["user_value"].asString(), "string");
		ensure("bar is null", preview["bar"]["user_value"].isNull());
	}

	TEST_METHOD(21) {
		set_test_name("forceApplyUpdatePreview()");

		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("bar", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);

		doc["foo"] = "string";
		doc["baz"] = true;

		Json::Value preview = config.previewUpdate(doc, errors);
		ensure_equals("1 error", errors.size(), 1u);
		ensure_equals(errors[0].getFullMessage(), "'bar' is required");

		config.forceApplyUpdatePreview(preview);
		ensure_equals("foo is a string", config["foo"].asString(), "string");
		ensure("bar is null", config["bar"].isNull());
	}

	TEST_METHOD(22) {
		set_test_name("dump()");

		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::REQUIRED);
		config.registerKey("bar", ConfigStore::INTEGER_TYPE, ConfigStore::REQUIRED);

		doc["foo"] = "string";
		doc["bar"] = 123;
		ensure(config.update(doc, errors));
		ensure(errors.empty());

		Json::Value dump = config.dump();
		ensure_equals("foo user value", dump["foo"]["user_value"].asString(), "string");
		ensure_equals("foo effective value", dump["foo"]["effective_value"].asString(), "string");
		ensure_equals("bar user value", dump["bar"]["user_value"].asInt(), 123);
		ensure_equals("bar effective value", dump["bar"]["effective_value"].asInt(), 123);
	}

	TEST_METHOD(23) {
		set_test_name("Default values");

		config.registerKey("foo", ConfigStore::STRING_TYPE, ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue("string"));
		config.registerKey("bar", ConfigStore::INTEGER_TYPE, ConfigStore::OPTIONAL,
			ConfigStore::staticDefaultValue(123));

		ensure_equals(config["foo"].asString(), "string");
		ensure_equals(config["bar"].asInt(), 123);

		Json::Value dump = config.dump();
		ensure("foo user value", dump["foo"]["user_value"].isNull());
		ensure_equals("foo default value", dump["foo"]["default_value"].asString(), "string");
		ensure_equals("foo effective value", dump["foo"]["effective_value"].asString(), "string");
		ensure("bar user value", dump["bar"]["user_value"].isNull());
		ensure_equals("bar default value", dump["bar"]["default_value"].asInt(), 123);
		ensure_equals("bar effective value", dump["bar"]["effective_value"].asInt(), 123);
	}
}
