//@brief debug step
//@description
//
//## 1th: analysis
//  I call unqlite_close and unqlite_open when commit.
//  this will force flush everything, and reload
//  as I found, all data have wrote to database file.
//
//  so the bug maybe inside unqlite_commit or before.
//
//  I have read SQLite3 source codes some years ago.
//  The SQLite3 pages cache is complex, so I guess maybe cache mismatch?
//  the debug step will be:
//  1. when flush (commit) to database, is all dirty pages delete?
//  1.1. If not, how unqlite do?
//  1.2. If yes, when it reload from disk?
//
//  2. is there any dirty pages not mark?
//  2.1. If not, why?
//  2.2. If yes, dump all pages for every change or read, and have a look.
//
//## 2nd: debuging
//  I analysis unqlite_commit, it merge dirty pages,
//  then wrote to database file, unref that pages when sync.
//  
//  and I try to log pages number,
//  such like `unqlite_kv_store` and `unqlite_kv_fetch`.
//
//  when update key `162`, unqlite_kv_store trigger pages overflow,
//  and reassign new pages, so maybe unqlite using old page after commit.
//
//  I add this codes to `pager_commit_phase1(Pager *pPager)`
//  /* release all pages */
//  {
//      Page *p;
//
//      while (1) {
//          p = pPager->pAll;
//          if (p == NULL) {
//              break;
//          }
//          pager_unlink_page(pPager, p);
//      }
//  }
//  /* release all pages */
//
//  and the bug gone.
//
//  I dump all pages number, have some try:
//  1. release page 7, the bug gone.
//  2. release page 1, buggy.
//
//  And I found `unqlite_kv_fetch` access page 7,
//  and trigger page 4 overflow to page 4 and page 5.
//
//
#include <string>
#include <ctime>
#include <cstdint>

extern "C" {
#include "unqlite.h"
}

struct TestStruct
{
    double d1;
    double d2;
};

#define WR_KEY    162
#define N         165

int main(int argc, char **argv)
{
#ifdef TEST_COMMIT
    std::string filename("TEST404." + std::to_string(time(nullptr)));
#else
    std::string filename("TEST200." + std::to_string(time(nullptr)));
#endif
    TestStruct test  = { 1.0, 1.1 };
    TestStruct test_modify = { 2.0, 1.1 };

    // Fill database 
    {
        unqlite *db = nullptr;

        int res = unqlite_open(&db, filename.c_str(), UNQLITE_OPEN_CREATE);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_open %d", res);
            return res;
        }

        for (int64_t i = 0; i < N; ++i)
        //for (int64_t i = N - 1; i >= 0; --i)
        {
            int res = unqlite_kv_store(db, &i, sizeof(int64_t), &test, sizeof(TestStruct));
            if (res != UNQLITE_OK)
            {
                printf("Error: unqlite_kv_store %d", res);
                return res;
            }
        }

        res = unqlite_close(db);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_close %d", res);
            return res;
        }
    }

    printf("\n\n\n");

    // Reopen
    {
        unqlite *db = nullptr;

        int res = unqlite_open(&db, filename.c_str(), UNQLITE_OPEN_CREATE);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_open %d", res);
            return res;
        }
        printf("\n");

        res = unqlite_begin(db);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_begin %d", res);
            return res;
        }
        printf("\n");

        // Write pair with key 162 again
        int64_t i_bug = 0;
        i_bug = WR_KEY;
        res = unqlite_kv_store(db, &i_bug, sizeof(int64_t), &test_modify, sizeof(TestStruct));
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_kv_store %d", res);
            return res;
        }
        printf("\n");

        //// Force commit
#ifdef TEST_COMMIT
        res = unqlite_commit(db);
#endif
        //res = unqlite_rollback(db);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_commit %d", res);
            return res;
        }
        printf("\n");


        //res = unqlite_close(db);
        //if (res != UNQLITE_OK)
        //{
        //    printf("Error: unqlite_close %d", res);
        //    return res;
        //}
        //res = unqlite_open(&db, filename.c_str(), UNQLITE_OPEN_CREATE);
        //if (res != UNQLITE_OK)
        //{
        //    printf("Error: unqlite_open %d", res);
        //    return res;
        //}


        TestStruct test1= {};
        unqlite_int64 buf_size = sizeof(TestStruct);

        //for (int64_t i = WR_KEY; i < N; ++i)
        for (int64_t i = 0; i < N; ++i)
        //for (int64_t i = 100; i < N; ++i)
        //for (int64_t i = N - 1; i >= 0; --i)
        {
            res = unqlite_kv_fetch(db, &i, sizeof(int64_t), &test1, &buf_size);
            if (res != UNQLITE_OK) {
                printf("!!! Error: key %d not found !!!\n", (int)i);
                break;
            }
        }

        res = unqlite_close(db);
        if (res != UNQLITE_OK)
        {
            printf("Error: unqlite_close %d", res);
            return res;
        }
    }

    return 0;
}
