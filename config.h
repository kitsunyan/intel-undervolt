#include <stdbool.h>

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define MEM_ADDR_TDP 0xfed159a0
#define MSR_ADDR_TDP 0x610
#define MSR_ADDR_TEMPERATURE 0x1a2
#define MSR_ADDR_UNITS 0x606
#define MSR_ADDR_VOLTAGE 0x150

typedef struct {
	void * next;
	int index;
	char * title;
	float value;
} uv_list_t;

typedef struct {
	int fd_msr;
	int fd_mem;
	uv_list_t * uv;
	bool tdp_apply;
	int tdp_short_term;
	float tdp_short_time_window;
	int tdp_long_term;
	float tdp_long_time_window;
	void * tdp_mem;
	bool tjoffset_apply;
	float tjoffset;
	int interval;
} config_t;

void uv_list_foreach(uv_list_t * uv,
	void (* callback)(uv_list_t *, void *), void * data);

void free_config(config_t * config);
config_t * load_config(config_t * old_config);
