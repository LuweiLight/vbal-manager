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
    double running_time;
    double runnnable_time;
    double blocked_time;
    double offline_time;
} runtime_info;

typedef struct {
    int dom_id;

    int weight;
    int cap;

    int max_vcpus;
    int online_vcpus;

    runtime_info prev;
    runtime_info cur;

    struct timeval last_check;
} dom_info;


typedef struct {
    string name;
    int num_cpus;
    int num_doms;
    int total_weight;
    unordered_map<int, dom_info> all_doms;
} cpupool_info;

typedef struct {
    int num_cpupools;
    unordered_map<string, cpupool_info> all_cpupools;
} vbal_manager;

vbal_manager global_manager;

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
            vbal.all_cpupools[cpupool_name].num_cpus = num_cpus;
        }
        else
        {
            cpupool_info new_cpupool;
            new_cpupool.num_cpus = num_cpus;
            new_cpupool.name = pool_name;
            vbal.all_cpupools.insert({pool_name, new_cpupool});
        }
    }

    vbal.num_cpupools = num_cpupools;
}

void get_cpupool_domains(cpupool_info &cpupool)
{
    string cmd("/usr/local/sbin/xl sched-credit-cpupool -p ");
    string cmd_p = cmd + cpupool.name;
    char line[256];
    int num_doms = 0;

    FILE *pipe = popen(cmd_p.c_str(), "r");

    fgets(line, sizeof(line), pipe);
    fgets(line, sizeof(line), pipe);

    char dom_name[256];
    int dom_id;
    int weight, cap;
    int max_vcpus, online_vcpus;
    double running_time, runnable_time, blocked_time, offline_time;

    while (fgets(line, sizeof(line), pipe) != NULL)
    {
        num_doms++;
        // printf("line = %s\n", line);
        sscanf(line, "%s %d %d %d %d %d %lf %lf %lf %lf",
                dom_name, &dom_id, &weight, &cap, &max_vcpus, &online_vcpus,
                &running_time, &runnable_time, &blocked_time, &offline_time);
        // printf("%s %d %d %d %d %d %lf %lf %lf %lf\n",
        //        dom_name, dom_id, weight, cap, max_vcpus, online_vcpus,
        //        running_time, runnable_time, blocked_time, offline_time);
    }
    
    pclose(pipe);
}

void get_all_domains(vbal_manager &vbal)
{
    for (auto iter = vbal.all_cpupools.begin(); iter != vbal.all_cpupools.end(); iter++)
    {
        get_cpupool_domains(iter->second);
    }
}

void print_manager(vbal_manager &vbal)
{
    printf("# of cpupools: %d\n", vbal.num_cpupools);
    for (auto iter = vbal.all_cpupools.begin(); iter != vbal.all_cpupools.end(); iter++)
    {
        printf("%s: %d CPUs\n", iter->first.c_str(), iter->second.num_cpus);
    }
}

int main(int argc, char* argv[])
{
    init_manager(global_manager);
    get_all_cpupools(global_manager);
    get_all_domains(global_manager);
    //print_manager(global_manager);

    return 0;
}

