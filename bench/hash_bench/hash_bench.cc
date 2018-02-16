#include <iostream>
#include <string>
#include <list>
#include <random>
#include "time_stat.h"

#include <libcuckoo/city_hasher.hh>
#include <libcuckoo/cuckoohash_map.hh>
#include <glib/glib.h>
#include "khash.h"
#include "uthash.h"
#include "Judy.h"

// C version of cuckoo hash.
#include "cuckoo_hash.h"

using namespace std;

KHASH_MAP_INIT_INT64(mu64, uint64_t);

struct u64_hash {
	uint64_t key;
	uint64_t value;
	UT_hash_handle hh;
};

#define KEY_POW 20
#define N_KEYS (2 << KEY_POW)
#define SEQ
//#undef SEQ

int main(void)
{
  /*
   * Initialization.
   */
  // C++
	cuckoohash_map<uint64_t, uint64_t, std::hash<uint64_t>> Table;
  // C
  struct cuckoo_hash ch_table;
	//cuckoohash_map<uint64_t, uint64_t, CityHasher<uint64_t>> Table;
	struct time_stats stats;
	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<uint64_t> dist(1, N_KEYS);
	std::list<uint64_t> key_list;

	for (uint64_t i = 0 ; i < N_KEYS/4; i++) {
		key_list.push_back(dist(mt));
	}

  if (!cuckoo_hash_init(&ch_table, KEY_POW)) {
    cout << "Not enough memory to init cuckoo hash." << endl;
    return -1;
  }

	cout << "# of keys: " << N_KEYS << endl;
	time_stats_init(&stats, 1);

  /*
   * Begin tests.
   */

	//////// Cuckoo hashing - C++
#if 1
#ifdef SEQ
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (uint64_t i = 0 ; i < N_KEYS; i++)
		Table.insert(i, i * 2);

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		uint64_t j;
		j = Table.find(i);

		if (j != (i*2)) {
			cout << "CUCKOO hash: value is wrong ";
			cout << i << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"CUCKOO HASH (C++ LANG) (SEQ) ---------------");
#else

	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (auto it: key_list) {
		Table.insert(it, it * 2);
	}

	for (auto it: key_list) {
		uint64_t j;
		j = Table.find(it);

		if (j != (it*2)) {
			cout << "CUCKOO hash: value is wrong ";
			cout << it << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"CUCKOO HASH (C++ LANG) (RAND) ---------------");
#endif
#endif

  // Cuckoo hashing -- C
#if 1
#ifdef SEQ
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (uint64_t i = 0; i < N_KEYS; i++) {
    uint64_t val = i*2;
    struct cuckoo_hash_item* res =
      cuckoo_hash_insert(&ch_table, i, val);
    if (res == CUCKOO_HASH_FAILED) {
      cerr << "Cannot insert into cuckoo hash table!" << endl;
      return -1;
    }
  }

	for (uint64_t i = 0; i < N_KEYS; i++) {
		struct cuckoo_hash_item* res =
      cuckoo_hash_lookup(&ch_table, i);

    if (!res) {
      cerr << "Key does not exist! " << i << endl;
      return -1;
    }
    uint64_t j = res->value;
		if (j != (i*2)) {
			cout << "CUCKOO hash: value is wrong ";
			cout << i << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"CUCKOO HASH (C LANG) (SEQ) ---------------");
#else

	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (auto it: key_list) {
    uint64_t* val = (uint64_t*)malloc(sizeof(*val));
    *val = it*2;
    struct cuckoo_hash_item* res =
      cuckoo_hash_insert(&ch_table, &it, sizeof(it), val);
    if (res == CUCKOO_HASH_FAILED) {
      cerr << "Cannot insert into cuckoo hash table!" << endl;
      return -1;
    }
	}

	for (auto it: key_list) {
		struct cuckoo_hash_item* res =
      cuckoo_hash_lookup(&ch_table, &it, sizeof(it));

    if (!res) {
      cerr << "Key does not exist! " << it << endl;
      return -1;
    }
    uint64_t j = *(uint64_t*)res->value;
		if (j != (it*2)) {
			cout << "CUCKOO hash: value is wrong ";
			cout << it << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"CUCKOO HASH (C LANG) (RAND) ---------------");
#endif
#endif

	//////// Glib hashing
#if 1
	GHashTable *direct_hash;

	direct_hash = g_hash_table_new(g_direct_hash, g_direct_equal);

	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

#ifdef SEQ
  // Don't insert a key 0.
	for (uint64_t i = 1; i <= N_KEYS; i++) {
		uint64_t j = (i * 2);

		g_hash_table_insert(direct_hash, GUINT_TO_POINTER(i), GUINT_TO_POINTER(j));
	}

	for (uint64_t i = 1; i <= N_KEYS; i++) {
    gpointer val = g_hash_table_lookup(direct_hash, GUINT_TO_POINTER(i));
    if (!val) {
      cerr << "Glib hash: could not find key " << i << endl;
    }

		uint64_t j = GPOINTER_TO_UINT(val);

		if (j != (i*2)) {
			cout << "Glib hash: value is wrong ";
			cout << i << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"GLIB HASH (SEQ) ---------------");
#else

	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (const uint64_t& it : key_list) {
		uint64_t j = it * 2;
		g_hash_table_insert(direct_hash, GUINT_TO_POINTER(it),
                                     GUINT_TO_POINTER(j));
	}

	for (const uint64_t& it : key_list) {
		gpointer val = g_hash_table_lookup(direct_hash, GUINT_TO_POINTER(it));

    if (!val) {
      cerr << "Glib hash: could not find key " << i << endl;
    }

		uint64_t j = GPOINTER_TO_UINT(val);

		if (j != (it*2)) {
			cout << "Glib hash: value is wrong ";
			cout << it << " " << j << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"GLIB HASH (RAND) ---------------");
#endif
#endif

	//////// KLIB hashing
#if 1
	int ret;
	khiter_t k_iter;
	khash_t(mu64) *klib_ht = kh_init(mu64);

#ifdef SEQ
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		k_iter = kh_put(mu64, klib_ht, i, &ret);
		if (ret == 0)
			cout << "KLIB hash: Key exists" << endl;
		kh_value(klib_ht, k_iter) = (i * 2);
	}

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		uint64_t value;
		k_iter = kh_put(mu64, klib_ht, i, &ret);
		value = kh_value(klib_ht, k_iter);

		if (value != (i * 2)) {
			cout << "KLIB hash: value is wrong ";
			cout << i << " " << value << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"KLIB HASH (SEQ) ---------------");
#else
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (auto it: key_list) {
		k_iter = kh_put(mu64, klib_ht, it, &ret);
		/*
		if (ret == 0)
			cout << "KLIB hash: Key exists" << endl;
		*/
		kh_value(klib_ht, k_iter) = (it * 2);
	}

	for (auto it: key_list) {
		uint64_t value;
		k_iter = kh_put(mu64, klib_ht, it, &ret);
		value = kh_value(klib_ht, k_iter);

		if (value != (it * 2)) {
			cout << "KLIB hash: value is wrong ";
			cout << it << " " << value << endl;
			exit(-1);
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"KLIB HASH (RAND) ---------------");
#endif
#endif

	//////// UT hashing
#if 1
	struct u64_hash *ut_hash = NULL;

#ifdef SEQ
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		struct u64_hash *entry;
		entry = (struct u64_hash *)malloc(sizeof(struct u64_hash));

		entry->key = i;
		entry->value = i * 2;
		HASH_ADD(hh, ut_hash, key, sizeof(uint64_t), entry);
	}

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		struct u64_hash *entry;

		HASH_FIND(hh, ut_hash, &i, sizeof(uint64_t), entry);
		if (!entry)
			cout << "UT hash: cannot find entry" << endl;
		if (entry->value != (i * 2)) {
			cout << "UT hash: value is wrong ";
			cout << i << " " << entry->value << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"UT HASH (SEQ) ---------------");
#else
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (auto it : key_list) {
		struct u64_hash *entry;
		entry = (struct u64_hash *)malloc(sizeof(struct u64_hash));

		entry->key = it;
		entry->value = it * 2;
		HASH_ADD(hh, ut_hash, key, sizeof(uint64_t), entry);
	}

	for (auto it : key_list) {
		struct u64_hash *entry;

		HASH_FIND(hh, ut_hash, &it, sizeof(uint64_t), entry);
		if (!entry)
			cout << "UT hash: cannot find entry" << endl;
		if (entry->value != (it * 2)) {
			cout << "UT hash: value is wrong ";
			cout << it << " " << entry->value << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"UT HASH (RAND) ---------------");
#endif
#endif
	//////// Judy Array
	// http://judy.sourceforge.net/doc/JudyL_3x.htm
#if 1
	Word_t index, value, *p_value;
	Pvoid_t jd_array = NULL;
	assert(sizeof(uint64_t) == sizeof(Word_t));
#ifdef SEQ
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		JLI(p_value, jd_array, i);
		*p_value = i * 2;
	}
	for (uint64_t i = 0 ; i < N_KEYS; i++) {
		JLG(p_value, jd_array, i);
		value = *p_value;

		if (value != (i * 2)) {
			cout << "Judy array: value is wrong ";
			cout << i << " " << value << endl;
		}
	}

	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"Judy array (SEQ) ---------------");
#else
	time_stats_reinit(&stats, 1);
	time_stats_start(&stats);

	for (auto it : key_list) {
		JLI(p_value, jd_array, it);
		*p_value = it * 2;
	}

	for (auto it : key_list) {
		JLG(p_value, jd_array, it);
		value = *p_value;

		if (value != (it * 2)) {
			cout << "Judy array: value is wrong ";
			cout << it << " " << value << endl;
		}
	}
	time_stats_stop(&stats);

	time_stats_print(&stats, (char *)"Judy array (Rand) ---------------");
#endif
#endif

	return 0;
}
