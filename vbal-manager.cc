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
    int dom_id;
	int max_vcpus;
	int online_vcpus;
	double running_time;
	double runnable_time;
	double blocked_time;
	double offline_time;
	struct timeval last_check;
} dom_info;


typedef struct {
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
            vbal.all_cpupools.insert({pool_name, new_cpupool});
        }
    }

    vbal.num_cpupools = num_cpupools;
}

void print_manager(vbal_manager &vbal)
{
	printf("# of cpupools: %d\n", vbal.num_cpupools);
    for (auto iter = vbal.all_cpupools.begin(); iter != vbal.all_cpupools.end(); iter++)
    {
        printf("%s: %d CPUs\n", iter->first.c_str(), iter->second.num_cpus);
    }
}


void get_cmd_output(void)
{
	char cmd[] = "/usr/local/sbin/xl list_cpu";
	char line[256];
	int num_dom = 0;

	FILE *pipe = popen(cmd, "r");

	// headline, ignore;
	fgets(line, sizeof(line), pipe);

	int dom_id;
	int max_vcpus;
	int online_vcpus;
	double running_time, runnable_time, blocked_time, offline_time;

	while (fgets(line, sizeof(line), pipe) != NULL) {
		num_dom++;
		printf("line = %s\n", line);

		sscanf(line, "%d %d %d %lf %lf %lf %lf",
                              &dom_id, &max_vcpus, &online_vcpus,
                              &running_time, &runnable_time, &blocked_time, &offline_time);

		printf("%d %d %d %lf %lf %lf %lf\n",
			dom_id, max_vcpus, online_vcpus,
			running_time, runnable_time, blocked_time, offline_time);
	}

	pclose(pipe);
}

int main(int argc, char* argv[])
{
	// get_cmd_output();

    init_manager(global_manager);
    get_all_cpupools(global_manager);
    print_manager(global_manager);

	return 0;
}

