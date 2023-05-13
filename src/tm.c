#include "date.h"
#include "janet.h"

// wrappers around struct tm

static JanetMethod jd_tm_methods[] = {
	// raw mktime, returning time_t
	{"mktime",     jd_mktime},
	{"mktime!",    jd_mktime_inplace},
	// shortcut for localtime(mktime)
	{"localtime",  jd_time_localtime},
	{"localtime!", jd_time_localtime_inplace},
	// shortcut for gmtime(mktime)
	{"utc",        jd_time_utc},
	{"utc!",       jd_time_utc_inplace},
	{"strftime",   jd_strftime},
	{NULL, NULL},
};

static int jd_tm_compare(void *lhs, void *rhs) {
	struct tm lhp = (*(struct tm*)lhs);
	struct tm rhp = (*(struct tm*)rhs);
	time_t lhv = mktime(&lhp);
	time_t rhv = mktime(&rhp);
	return difftime(lhv, rhv);
}

// C99 specifies ALL fields to be int
struct jd_tm_key {
	const char  *key;
	const size_t off;
};
static const struct jd_tm_key jd_tm_keys[] = {
	{"sec",   offsetof(struct tm, tm_sec)},
	{"min",   offsetof(struct tm, tm_min)},
	{"hour",  offsetof(struct tm, tm_hour)},
	{"mday",  offsetof(struct tm, tm_mday)},
	{"mon",   offsetof(struct tm, tm_mon)},
	{"year",  offsetof(struct tm, tm_year)},
	{"wday",  offsetof(struct tm, tm_wday)},
	{"yday",  offsetof(struct tm, tm_yday)},
	{"isdst", offsetof(struct tm, tm_isdst)},
#ifdef TM_GMTOFF
	{"gmtoff", offsetof(struct tm, TM_GMTOFF)},
#endif
#ifdef TM_ZONE
	{"zone", offsetof(struct tm, TM_ZONE)},
#endif
	{NULL, 0},
};

static int jd_tm_get(void *p, Janet key, Janet *out) {
	if (!janet_checktype(key, JANET_KEYWORD)) {
		return 0;
	}

	// is it a method?
	if(janet_getmethod(janet_unwrap_keyword(key), jd_tm_methods, out)) {
		return 1;
	}

	const struct jd_tm_key *ptr = jd_tm_keys;
	while (ptr->key) {
		if (janet_keyeq(key, ptr->key)) {
#ifdef TM_GMTOFF
			if (janet_keyeq(key, "gmtoff")) {
				long val = *((long*)(p + ptr->off));
				*out = janet_wrap_s64(val);
				return 1;
			}
#endif
#ifdef TM_ZONE
			if (janet_keyeq(key, "zone")) {
				const char *val = *((const char **)(p + ptr->off));
				*out = janet_ckeywordv(val);
				return 1;
			}
#endif

			int val = *((int*)(p + ptr->off));

			// exceptional values
			if (janet_keyeq(key, "year")) {
				val += 1900;
			} else if (janet_keyeq(key, "isdst")) {
				*out = val ? (val > 0 ? janet_wrap_true() : janet_ckeywordv("detect")) : janet_wrap_false();
				return 1;
			}

			*out = janet_wrap_integer(val);
			return 1;
		}
		ptr++;
	}

	return janet_checktype(*out, JANET_NIL);
}
static Janet jd_tm_next(void *p, Janet key) {
	(void) p;
	const struct jd_tm_key *ptr = jd_tm_keys;
	while (ptr->key) {
		if (janet_keyeq(key, ptr->key)) {
			return (++ptr)->key ? janet_ckeywordv(ptr->key) : janet_wrap_nil();
		}
		ptr++;
	}
	return janet_ckeywordv(jd_tm_keys[0].key);
}

static void jd_tm_put(void *data, Janet key, Janet value) {
	if (0
#ifdef TM_GMTOFF
		|| janet_keyeq(key, "gmtoff")
#endif
#ifdef TM_ZONE
		|| janet_keyeq(key, "zone")
#endif
	   ) {
		janet_panicf("%s is read-only", key);
	}
	// note that keyword, boolean are only valid for isdst
	if (!janet_checktypes(value, JANET_TFLAG_NUMBER | JANET_TFLAG_KEYWORD | JANET_TFLAG_BOOLEAN)) {
		janet_panicf("expected function or number, got %t", value);
	}
	const struct jd_tm_key *ptr = jd_tm_keys;
	while (ptr->key) {
		if (janet_keyeq(key, ptr->key)) {
			int *loc = (int*)(data + ptr->off);
			if (janet_keyeq(key, "year")) {
				*loc = janet_unwrap_integer(value) - 1900;
			} else if (janet_keyeq(key, "isdst")) {
				*loc = janet_keyeq(value, "detect") ? -1 : (janet_truthy(value) ? 1 : 0);
			} else {
				*loc = janet_unwrap_integer(value);
			}
			return;
		}
		ptr++;
	}

	janet_panicf("tried to write to invalid field: %v", key);
}

#define MAX_INT_STRLEN 1024
static void jd_tm_tostring(void *p, JanetBuffer *buffer) {
	janet_buffer_push_cstring(buffer, "{");
	char buf[MAX_INT_STRLEN];

	const struct jd_tm_key *ptr = jd_tm_keys;
	while (ptr->key) {
		void *loc = (void*)(p + ptr->off);
		janet_buffer_push_cstring(buffer, ":");
		janet_buffer_push_cstring(buffer, ptr->key);
		janet_buffer_push_cstring(buffer, " ");

		// exceptional values
		if (!strcmp(ptr->key, "year")) {
			snprintf(buf, MAX_INT_STRLEN, "%d", *(int*)loc + 1900);
		} else if (!strcmp(ptr->key, "isdst")) {
			strcpy(buf, *(int*)loc ? (*(int*)loc > 0 ? "true" : ":detect") : "false");
#ifdef TM_ZONE
		} else if (!strcmp(ptr->key, "zone")) {
			strcpy(buf, *(char**)loc);
#endif
#ifdef TM_GMTOFF
		} else if (!strcmp(ptr->key, "gmtoff")) {
			snprintf(buf, MAX_INT_STRLEN, "%ld", *(long*)loc);
#endif
		} else {
			snprintf(buf, MAX_INT_STRLEN, "%d", *(int*)loc);
		}
		janet_buffer_push_cstring(buffer, buf);

		if ((++ptr)->key) janet_buffer_push_cstring(buffer, " ");
	}

	janet_buffer_push_cstring(buffer, "}");
}

static const JanetAbstractType jd_tm_t = {
	"tm",
	NULL,
	NULL,
	jd_tm_get,
	jd_tm_put,
	NULL,
	NULL,
	jd_tm_tostring,
	jd_tm_compare,
	NULL,
	jd_tm_next,
	JANET_ATEND_NEXT
};

struct tm *jd_gettm(Janet *argv, int32_t n) {
	return (struct tm*)janet_getabstract(argv, n, &jd_tm_t);
}

struct tm *jd_maketm(void) {
	return janet_abstract(&jd_tm_t, sizeof(struct tm));
}

struct tm *jd_opttm(Janet *argv, int32_t argc, int32_t n) {
    if (n >= argc || janet_checktype(argv[n], JANET_NIL)) {
		return NULL;
	}
	return jd_gettm(argv, n);
}

JANET_FN(jd_mktime,
		"",
		"") {
	janet_fixarity(argc, 1);
	struct tm *tm = jd_gettm(argv, 0);
	struct tm *nw = jd_maketm();
	*nw = *tm;
	time_t *time = jd_maketime();
	*time = mktime(nw);
	return janet_wrap_abstract(time);
}

JANET_FN(jd_mktime_inplace,
		"",
		"") {
	janet_fixarity(argc, 1);
	struct tm *tm = jd_gettm(argv, 0);
	time_t *time = jd_maketime();
	*time = mktime(tm);
	return janet_wrap_abstract(time);
}

JANET_FN(jd_time_utc,
		"",
		"") {
	janet_arity(argc, 0, 1);
	struct tm *tm = jd_opttm(argv, argc, 0);
	struct tm *nw = jd_maketm();
	time_t t;
	if (tm) {
		*nw = *tm;
		t = mktime(nw);
	} else {
		t = time(NULL);
	}
	*nw = *(gmtime(&t));
	return janet_wrap_abstract(nw);
}

JANET_FN(jd_time_utc_inplace,
		"",
		"") {
	janet_fixarity(argc, 1);
	struct tm *tm = jd_gettm(argv, 0);
	time_t t = mktime(tm);
	*tm = *(gmtime(&t));
	return janet_wrap_abstract(tm);
}

JANET_FN(jd_time_localtime,
		"",
		"") {
	janet_arity(argc, 0, 1);
	struct tm *tm = jd_opttm(argv, argc, 0);
	struct tm *nw = jd_maketm();
	time_t t;
	if (tm) {
		*nw = *tm;
		t = mktime(nw);
	} else {
		t = time(NULL);
	}
	*nw = *(localtime(&t));
	return janet_wrap_abstract(nw);
}

JANET_FN(jd_time_localtime_inplace,
		"",
		"") {
	janet_fixarity(argc, 1);
	struct tm *tm = jd_gettm(argv, 0);
	time_t t = mktime(tm);
	*tm = *(localtime(&t));
	return janet_wrap_abstract(tm);
}

// strftime
struct strftime_format {
	const char *keyword;
	const char *format;
};
const static struct strftime_format strftime_formats[] = {
	{NULL, NULL},
};
JANET_FN(jd_strftime,
		"",
		"") {
	janet_fixarity(argc, 2);
	// tm is first for pseudo-OO
	struct tm *tm = jd_gettm(argv, 0);

	// determine format
	const char *format = NULL;

	// is it a preset?
	if (janet_checktype(argv[1], JANET_KEYWORD)) {
		const struct strftime_format *ptr = strftime_formats;
		while (ptr->keyword) {
			if (janet_keyeq(argv[1], ptr->keyword)) {
				format = ptr->format;
				break;
			}
			ptr++;
		}
	}

	// preset not found
	if (!format) format = janet_getcbytes(argv, 1);
	return janet_wrap_buffer(strftime_buffer(format, tm, NULL));
}

const JanetRegExt jd_tm_cfuns[] = {
	JANET_REG("mktime",   jd_mktime),
	JANET_REG("mktime!",  jd_mktime_inplace),
	JANET_REG("strftime", jd_strftime),
	JANET_REG_END
};
