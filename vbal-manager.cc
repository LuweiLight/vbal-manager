#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
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
    unsigned int dom_id;

    unsigned int weight;
    unsigned int cap;

    unsigned int max_vcpus;
    unsigned int online_vcpus;

    runtime_info prev_rt;
    runtime_info cur_rt;

    unsigned long long last_update_clock_usec;
    unsigned long long last_hotplug_clock_usec;

    bool is_new_dom;
} dom_info;


typedef struct {
    string name;
    unsigned int num_cpus;
    unsigned int num_doms;

    unsigned int total_weight;

    float prev_running_time;
    float cur_running_time;

    // dom_id -> dom_info
    unordered_map<unsigned int, dom_info> all_doms;

    // # of offline_vcpus -> dom_id; order >
    unordered_set<unsigned int> hotplug_doms_set;
    multimap<unsigned int, unsigned int, greater<unsigned int>> hotplug_doms_rank;

    unsigned long long last_update_clock_usec;

    bool is_new_cpupool;
    bool new_dom_join;
} cpupool_info;

typedef struct {
    unsigned int num_cpupools;
    unordered_map<string, cpupool_info> all_cpupools;
} vbal_manager;

/* 
 * Global variables etc. 
 */
vbal_manager global_manager;
unsigned long long global_update_clock_usec;
#define USEC_PER_SEC        1000000U
#define UPDATE_INTERVAL_SEC 5

void init_manager(vbal_manager &vbal)
{
    global_manager.num_cpupools = 0;
    global_manager.all_cpupools.clear();
}

void init_cpupool(cpupool_info &cpupool)
{
    cpupool.name.clear();
    cpupool.num_doms = 0U;
    cpupool.total_weight = 0U;

    cpupool.prev_running_time = 0.0;
    cpupool.cur_running_time = 0.0;

    cpupool.all_doms.clear();
    cpupool.hotplug_doms_set.clear();
    cpupool.hotplug_doms_rank.clear();
    cpupool.last_update_clock_usec = 0U;
    cpupool.is_new_cpupool = true;
    cpupool.new_dom_join = false;
}

void init_domain(dom_info &dom)
{
    dom.dom_id = INT_MAX;
    dom.weight = 0U;
    dom.cap = 0U;
    dom.max_vcpus = 0U;
    dom.online_vcpus = 0U;

    dom.prev_rt.running_time = 0.0;
    dom.prev_rt.runnable_time = 0.0;
    dom.prev_rt.blocked_time = 0.0;
    dom.prev_rt.offline_time = 0.0;
    dom.cur_rt.running_time = 0.0;
    dom.cur_rt.runnable_time = 0.0;
    dom.cur_rt.blocked_time = 0.0;
    dom.cur_rt.offline_time = 0.0;

    dom.last_update_clock_usec = 0U;
    dom.last_hotplug_clock_usec = 0U;
    dom.is_new_dom = true;
}

void get_all_cpupools(vbal_manager &vbal)
{
    char cmd[] = "/usr/local/sbin/xl cpupool-list";
    FILE *pipe = popen(cmd, "r");

    char line[256];
    fgets(line, sizeof(line), pipe); // headline, ignore;

    unsigned int num_cpupools = 0;
    while (fgets(line, sizeof(line), pipe) != NULL) 
    {
        num_cpupools++;

        char cpupool_name[256];
        unsigned int num_cpus;
        sscanf(line, "%s %u", cpupool_name, &num_cpus);
        // printf("%s, %d\n", cpupool_name, num_cpus);

        string pool_name(cpupool_name);
        if (vbal.all_cpupools.find(cpupool_name) != vbal.all_cpupools.end())
        {
            cpupool_info *orig_pool = &vbal.all_cpupools[cpupool_name];

            orig_pool->num_cpus = num_cpus;
            orig_pool->last_update_clock_usec = global_update_clock_usec;
            orig_pool->is_new_cpupool = false;
        }
        else /* A new cpupool joins. */
        {
            cpupool_info new_cpupool;
            init_cpupool(new_cpupool);

            new_cpupool.num_cpus = num_cpus;
            new_cpupool.name = pool_name;
            new_cpupool.last_update_clock_usec = global_update_clock_usec;

            vbal.all_cpupools.insert({pool_name, new_cpupool});
        }
    }

    vbal.num_cpupools = num_cpupools;

    pclose(pipe);
}

void get_cpupool_domains(cpupool_info &pool)
{
    string cmd("/usr/local/sbin/xl sched-credit-cpupool -p ");
    string cmd_p = cmd + pool.name;
    char line[256];
    unsigned int num_doms = 0U;

    FILE *pipe = popen(cmd_p.c_str(), "r");

    fgets(line, sizeof(line), pipe);
    fgets(line, sizeof(line), pipe);

    char dom_name[256];

    float pool_running_time = 0.0;
    unsigned int pool_total_weight = 0U;
    bool new_dom_join = false;

    while (fgets(line, sizeof(line), pipe) != NULL) 
    {
        // printf("%s\n", line);
        dom_info dom;
        init_domain(dom);

        sscanf(line, "%s %u %u %u %u %u %f %f %f %f",
                dom_name, 
                &dom.dom_id,
                &dom.weight, &dom.cap, 
                &dom.max_vcpus, &dom.online_vcpus,
                &dom.cur_rt.running_time, 
                &dom.cur_rt.runnable_time, 
                &dom.cur_rt.blocked_time, 
                &dom.cur_rt.offline_time);
/* 
        printf("%s %u %u %u %u %u %lf %f %f %f\n",
                dom_name, 
                dom.dom_id, 
                dom.weight, dom.cap, 
                dom.max_vcpus, 
                dom.online_vcpus,
                dom.cur_rt.running_time, 
                dom.cur_rt.runnable_time, 
                dom.cur_rt.blocked_time, 
                dom.cur_rt.offline_time);
*/

        if (pool.all_doms.find(dom.dom_id) != pool.all_doms.end())
        {
            dom_info *orig_dom = &pool.all_doms[dom.dom_id];

            orig_dom->dom_id = dom.dom_id;

            orig_dom->weight = dom.weight;
            orig_dom->cap = dom.cap;

            orig_dom->cur_rt.running_time = dom.cur_rt.running_time;
            orig_dom->cur_rt.runnable_time = dom.cur_rt.runnable_time;
            orig_dom->cur_rt.blocked_time = dom.cur_rt.blocked_time;
            orig_dom->cur_rt.offline_time = dom.cur_rt.offline_time;

            orig_dom->last_update_clock_usec = global_update_clock_usec;
            orig_dom->is_new_dom = false;
        }
        else /* A new domain joins. */
        {
            /* Important: the domain just boots up */
            if (dom.max_vcpus == 0)
            {
                new_dom_join = true;
                continue;
            }
            dom.last_update_clock_usec = global_update_clock_usec;

            pool.all_doms.insert({dom.dom_id, dom});

            // printf("New domain: dom %u, max:%u, online:%u\n",
            //        dom.dom_id, dom.max_vcpus, dom.online_vcpus);

            unsigned int offline_vcpus = dom.max_vcpus - dom.online_vcpus;
            if ( offline_vcpus != 0U)
            {
                pool.hotplug_doms_set.insert(dom.dom_id);
                pool.hotplug_doms_rank.insert({offline_vcpus, dom.dom_id});
            }
        }

        num_doms++;
        pool_running_time += dom.cur_rt.running_time;
        pool_total_weight += dom.weight;
    }
    pool.num_doms = num_doms;
    pool.cur_running_time = pool_running_time;
    pool.total_weight = pool_total_weight;
    pool.new_dom_join = new_dom_join;

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

        // This cpupool disappears.
        if (pool->last_update_clock_usec < global_update_clock_usec)
        {
            pool_iter = vbal.all_cpupools.erase(pool_iter);
            continue;
        }

        // see whether the cpupool has been fully utilized
        // If YES - nothing to to; 
        // If NO - we may add more vCPUs (if possible);
        if ( !pool->is_new_cpupool && !pool->new_dom_join)
        {
            float pool_running_time = pool->cur_running_time - pool->prev_running_time;
            float pool_allocated_time = pool->num_cpus * UPDATE_INTERVAL_SEC;
            float pool_idle_time = fabs(pool_allocated_time - pool_running_time) * 1.05;
            unsigned int num_idle_cpus = (unsigned int)(pool_idle_time / UPDATE_INTERVAL_SEC);

            printf("-cpupool %s, idle_time = %.2f, idle_cpus = %u\n", 
                    pool->name.c_str(), pool_idle_time, num_idle_cpus);

            for (unsigned int i = 0; i < num_idle_cpus; i++)
            {
                // Try to add one vCPU at a time
                if ( !pool->hotplug_doms_set.empty() )
                {
                    /*
                    for (auto dom_iter = pool->hotplug_doms_rank.begin();
                              dom_iter != pool->hotplug_doms_rank.end();
                              dom_iter++)
                    {
                        unsigned int dom_id = dom_iter->second;
                        printf("---offline_vcpus: %u, dom_id = %u, max = %u, online = %u\n",
                                dom_iter->first, dom_id,
                                pool->all_doms[dom_id].max_vcpus,
                                pool->all_doms[dom_id].online_vcpus);
                    }
                    */

                    auto dom_iter = pool->hotplug_doms_rank.begin();
                    unsigned int dom_id = dom_iter->second;
                    pool->hotplug_doms_rank.erase(dom_iter);
                    pool->hotplug_doms_set.erase(dom_id);

                    dom_info *dom = &pool->all_doms[dom_id];
                    printf("Add-vCPU: dom %u, max: %u online: %u\n", dom->dom_id, dom->max_vcpus, dom->online_vcpus);

                    assert(dom->online_vcpus < dom->max_vcpus);
                    dom->online_vcpus++;
                    dom->last_hotplug_clock_usec = global_update_clock_usec;

                    string cmd("/usr/local/sbin/xl vcpu-set ");
                    cmd += to_string((long long)dom->dom_id);
                    cmd += " ";
                    cmd += to_string((long long)dom->online_vcpus);

                    printf("exec: %s\n", cmd.c_str());
                    system(cmd.c_str());

                    unsigned int offline_vcpus = dom->max_vcpus - dom->online_vcpus;
                    if (offline_vcpus != 0U)
                    {
                        pool->hotplug_doms_set.insert(dom_id);
                        pool->hotplug_doms_rank.insert({offline_vcpus, dom_id});
                    }
                }
                else
                {
                    break;
                }
            }
        }

        pool->prev_running_time = pool->cur_running_time;


        for (auto dom_iter = pool->all_doms.begin();
                  dom_iter != pool->all_doms.end(); )
        {
            dom_info *dom = &dom_iter->second;

            // This domain disappears.
            if (dom->last_update_clock_usec < global_update_clock_usec)
            {
                dom_iter = pool->all_doms.erase(dom_iter);
                continue;
            }

            if ( !pool->is_new_cpupool && 
                 dom->dom_id != 0U && 
                 !dom->is_new_dom &&
                 dom->last_hotplug_clock_usec < global_update_clock_usec)
            {
                float dom_steal_time = (dom->cur_rt.runnable_time + dom->cur_rt.offline_time) -
                                       (dom->prev_rt.runnable_time + dom->prev_rt.offline_time);
                unsigned int offline_vcpus = (unsigned int)(dom_steal_time * 1.05 / UPDATE_INTERVAL_SEC);
                
                printf("dom %u, steal_time = %.2f, max_vcpus = %u, online_vcpus = %u, offline_vcpus = %u\n",
                        dom->dom_id, dom_steal_time, dom->max_vcpus, dom->online_vcpus, offline_vcpus);

                if ( offline_vcpus > (dom->max_vcpus - dom->online_vcpus) )
                {
                    dom->online_vcpus = dom->max_vcpus - offline_vcpus;
                    printf("Remove-vCPU: dom %u, max: %u online: %u\n", 
                            dom->dom_id, dom->max_vcpus, dom->online_vcpus);

                    string cmd("/usr/local/sbin/xl vcpu-set ");
                    cmd += to_string((long long)dom->dom_id);
                    cmd += " ";
                    cmd += to_string((long long)dom->online_vcpus);

                    printf("exec: %s\n", cmd.c_str());
                    system(cmd.c_str());

                    if ( pool->hotplug_doms_set.find(dom->dom_id) == pool->hotplug_doms_set.end() )
                    {
                        pool->hotplug_doms_set.insert(dom->dom_id);
                        pool->hotplug_doms_rank.insert({offline_vcpus, dom->dom_id});
                    }
                }
                else if ( offline_vcpus < (dom->max_vcpus - dom->online_vcpus) )
                {
                    printf("Error: dom %u, steal_time = %.2f, max_vcpus = %u, online_vcpus = %u\n", 
                           dom->dom_id, dom_steal_time, dom->max_vcpus, dom->online_vcpus);
                }
            }

            dom->prev_rt.running_time = dom->cur_rt.running_time;
            dom->prev_rt.runnable_time = dom->cur_rt.runnable_time;
            dom->prev_rt.blocked_time = dom->cur_rt.blocked_time;
            dom->prev_rt.offline_time = dom->cur_rt.offline_time;

            dom_iter++;
        }

        pool_iter++;
    }
}

void print_manager(vbal_manager &vbal)
{
    printf("===============================\n");
    printf("# of cpupools: %u\n", vbal.num_cpupools);
    for (auto pool_iter = vbal.all_cpupools.begin();
              pool_iter != vbal.all_cpupools.end(); 
              pool_iter++)
    {
        cpupool_info *pool = &pool_iter->second;
        printf("[%llu] = CPUPool = %15s: %u CPUs, %u dom(s) <r:%.2f/%.2f>.\n", 
                pool->last_update_clock_usec, pool_iter->first.c_str(),
                pool->num_cpus, pool->num_doms,
                pool->cur_running_time, 
                pool->prev_running_time
              );

        for (auto dom_iter = pool->all_doms.begin(); 
                  dom_iter != pool->all_doms.end(); 
                  dom_iter++)
        {
            dom_info *dom = &dom_iter->second;
            printf("[%llu] dom %u: <w:%u c:%u> <max:%u online:%u> <r:%.2f/%.2f w:%.2f/%.2f b:%.2f/%.2f o:%.2f/%.2f>\n",
                    dom->last_update_clock_usec,
                    dom->dom_id,
                    dom->weight, dom->cap,
                    dom->max_vcpus, dom->online_vcpus,
                    dom->cur_rt.running_time, dom->prev_rt.running_time,
                    dom->cur_rt.runnable_time, dom->prev_rt.runnable_time,
                    dom->cur_rt.blocked_time, dom->prev_rt.blocked_time,
                    dom->cur_rt.offline_time, dom->prev_rt.offline_time
                   );
        }

        for (auto dom_iter = pool->hotplug_doms_rank.begin();
                  dom_iter != pool->hotplug_doms_rank.end();
                  dom_iter++)
        {
            unsigned int dom_id = dom_iter->second;
            printf("---offline_vcpus: %u, dom_id = %u, max = %u, online = %u\n",
                   dom_iter->first, dom_id,
                   pool->all_doms[dom_id].max_vcpus,
                   pool->all_doms[dom_id].online_vcpus);
        }
    }
}

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

