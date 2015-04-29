#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

using namespace std;

typedef struct {
    float running_time;
    float runnable_time;
    float blocked_time;
    float offline_time;
} runtime_info;

typedef struct {
    int dom_id;

    int weight;
    int cap;

    int max_vcpus;
    int online_vcpus;

    runtime_info prev;
    runtime_info cur;

    long long last_update_clock_usec;
} dom_info;


typedef struct {
    string name;
    int num_cpus;
    int num_doms;
    int total_weight;
    unordered_map<int, dom_info> all_doms;

    long long last_update_clock_usec;
} cpupool_info;

typedef struct {
    int num_cpupools;
    unordered_map<string, cpupool_info> all_cpupools;
} vbal_manager;

vbal_manager global_manager;
long long global_update_clock_usec;

void init_manager(vbal_manager &vbal)
{
    global_manager.num_cpupools = 0;
    global_manager.all_cpupools.clear();
}

void get_all_cpupools(vbal_manager &vbal)
{
    char cmd[] = "/usr/local/sbin/xl cpupool-list";
    FILE *pipe = popen(cmd, "r");

    char line[256];
    fgets(line, sizeof(line), pipe); // headline, ignore;

    int num_cpupools = 0;
    while (fgets(line, sizeof(line), pipe) != NULL) 
    {
        num_cpupools++;

        char cpupool_name[256];
        int num_cpus;
        sscanf(line, "%s %d", cpupool_name, &num_cpus);
        // printf("%s, %d\n", cpupool_name, num_cpus);

        string pool_name(cpupool_name);
        if (vbal.all_cpupools.find(cpupool_name) != vbal.all_cpupools.end())
        {
            cpupool_info *orig_pool = &vbal.all_cpupools[cpupool_name];

            orig_pool->num_cpus = num_cpus;
            orig_pool->last_update_clock_usec = global_update_clock_usec;
        }
        else
        {
            cpupool_info new_cpupool;
            new_cpupool.num_cpus = num_cpus;
            new_cpupool.name = pool_name;
            new_cpupool.last_update_clock_usec = global_update_clock_usec;
            vbal.all_cpupools.insert({pool_name, new_cpupool});
        }
    }

    vbal.num_cpupools = num_cpupools;
}

void get_cpupool_domains(cpupool_info &pool)
{
    string cmd("/usr/local/sbin/xl sched-credit-cpupool -p ");
    string cmd_p = cmd + pool.name;
    char line[256];
    int num_doms = 0;

    FILE *pipe = popen(cmd_p.c_str(), "r");

    fgets(line, sizeof(line), pipe);
    fgets(line, sizeof(line), pipe);

    char dom_name[256];

    while (fgets(line, sizeof(line), pipe) != NULL) 
    {
        num_doms++;
        // printf("%s\n", line);
        dom_info dom;
        sscanf(line, "%s %d %d %d %d %d %f %f %f %f",
                dom_name, 
                &dom.dom_id,
                &dom.weight, &dom.cap, 
                &dom.max_vcpus, &dom.online_vcpus,
                &dom.cur.running_time, 
                &dom.cur.runnable_time, 
                &dom.cur.blocked_time, 
                &dom.cur.offline_time);
        /*
        printf("%s %d %d %d %d %d %lf %f %f %f\n",
                dom_name, 
                dom.dom_id, 
                dom.weight, dom.cap, 
                dom.max_vcpus, dom.online_vcpus,
                dom.cur.running_time, 
                dom.cur.runnable_time, 
                dom.cur.blocked_time, 
                dom.cur.offline_time);
        */

        if (pool.all_doms.find(dom.dom_id) != pool.all_doms.end())
        {
            dom_info *orig_dom = &pool.all_doms[dom.dom_id];

            orig_dom->dom_id = dom.dom_id;
            orig_dom->weight = dom.weight;
            orig_dom->cap = dom.cap;
            orig_dom->max_vcpus = dom.max_vcpus;
            orig_dom->online_vcpus = dom.online_vcpus;
            orig_dom->cur.running_time = dom.cur.running_time;
            orig_dom->cur.runnable_time = dom.cur.runnable_time;
            orig_dom->cur.blocked_time = dom.cur.blocked_time;
            orig_dom->cur.offline_time = dom.cur.offline_time;

            orig_dom->last_update_clock_usec = global_update_clock_usec;
        }
        else 
        {
            dom.prev.running_time = 0.0;
            dom.prev.runnable_time = 0.0;
            dom.prev.blocked_time = 0.0;
            dom.prev.offline_time = 0.0;

            dom.last_update_clock_usec = global_update_clock_usec;

            pool.all_doms.insert({dom.dom_id, dom});
        }
    }
    pool.num_doms = num_doms;

    pclose(pipe);
}

void get_all_domains(vbal_manager &vbal)
{
    for (auto pool_iter = vbal.all_cpupools.begin(); 
              pool_iter != vbal.all_cpupools.end(); 
              pool_iter++)
    {
        get_cpupool_domains(pool_iter->second);
    }
}

void check_all_domains(vbal_manager &vbal)
{
    for (auto pool_iter = vbal.all_cpupools.begin(); 
              pool_iter != vbal.all_cpupools.end(); )
    {
        cpupool_info *pool = &pool_iter->second;
        if (pool->last_update_clock_usec < global_update_clock_usec)
        {
            pool_iter = vbal.all_cpupools.erase(pool_iter);
            continue;
        }

        for (auto dom_iter = pool->all_doms.begin();
                  dom_iter != pool->all_doms.end(); )
        {
            dom_info *dom = &dom_iter->second;
            if (dom->last_update_clock_usec < global_update_clock_usec)
            {
                dom_iter = pool->all_doms.erase(dom_iter);
                continue;
            }

            // check steal_time here.
            dom->prev.running_time = dom->cur.running_time;
            dom->prev.runnable_time = dom->cur.runnable_time;
            dom->prev.blocked_time = dom->cur.blocked_time;
            dom->prev.offline_time = dom->cur.offline_time;

            dom_iter++;
        }

        pool_iter++;
    }
}

void print_manager(vbal_manager &vbal)
{
    printf("# of cpupools: %d\n", vbal.num_cpupools);
    for (auto pool_iter = vbal.all_cpupools.begin();
              pool_iter != vbal.all_cpupools.end(); 
              pool_iter++)
    {
        cpupool_info *pool = &pool_iter->second;
        printf("[%lld] = CPUPool = %15s: %d CPUs, %d domain(s).\n", 
                pool->last_update_clock_usec, pool_iter->first.c_str(),
                pool->num_cpus, pool->num_doms);

        for (auto dom_iter = pool->all_doms.begin(); 
                  dom_iter != pool->all_doms.end(); 
                  dom_iter++)
        {
            dom_info *dom = &dom_iter->second;
            printf("[%lld] dom %d: <w:%d c:%d> <max:%d online:%d> <r:%.2f/%.2f w:%.2f/%.2f b:%.2f/%.2f o:%.2f/%.2f>\n",
                    dom->last_update_clock_usec,
                    dom->dom_id,
                    dom->weight, dom->cap,
                    dom->max_vcpus, dom->online_vcpus,
                    dom->cur.running_time, dom->prev.running_time,
                    dom->cur.runnable_time, dom->prev.runnable_time,
                    dom->cur.blocked_time, dom->prev.blocked_time,
                    dom->cur.offline_time, dom->prev.offline_time
                   );
        }
    }
}

#define USEC_PER_SEC 1000000U
#define UPDATE_INTERVAL_SEC 5

int main(int argc, char* argv[])
{
    init_manager(global_manager);

    while (1)
    {
        struct timeval timestamp;
        gettimeofday(&timestamp, NULL);
        global_update_clock_usec = timestamp.tv_sec * 1000000 + timestamp.tv_usec;
        get_all_cpupools(global_manager);
        get_all_domains(global_manager);

        print_manager(global_manager);

        check_all_domains(global_manager);
        usleep(UPDATE_INTERVAL_SEC * USEC_PER_SEC);
    }

    return 0;
}

