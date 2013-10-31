#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "node.h"
#include "route.h"
#include "../data_collector/rpl_event_callbacks.h"
#include "rpl_data.h"
#include "rpl_def.h"
#include "6lowpan_def.h"

struct di_node {
	di_node_key_t key;
	uint16_t simple_id;

	di_dodag_ref_t dodag;

    bool has_changed;
    int has_errors;

    sixlowpan_config_t sixlowpan_config;
    sixlowpan_config_delta_t sixlowpan_config_delta;

	bool has_rpl_instance_config;
    rpl_instance_config_t rpl_instance_config;               //Via DIO
    rpl_instance_config_delta_t rpl_instance_config_delta;
    rpl_instance_config_delta_t actual_rpl_instance_config_delta;

    bool has_rpl_instance_data;
    rpl_instance_data_t rpl_instance_data;               //Via DIO, DAO, RPL option
    rpl_instance_data_delta_t rpl_instance_data_delta;

    bool has_rpl_dodag_config;
    rpl_dodag_config_t rpl_dodag_config;               //Via DIO config option
	rpl_dodag_config_delta_t rpl_dodag_config_delta;
    rpl_dodag_config_delta_t actual_rpl_dodag_config_delta;

	bool has_rpl_dodag_prefix_info;
	rpl_prefix_t rpl_dodag_prefix_info;                //Via DIO prefix option
    rpl_prefix_delta_t rpl_dodag_prefix_info_delta;
    rpl_prefix_delta_t actual_rpl_dodag_prefix_info_delta;

    bool routes_delta;
	di_route_list_t routes;

	//statistics
	sixlowpan_statistics_t sixlowpan_statistics;
	sixlowpan_statistics_delta_t sixlowpan_statistics_delta;

	rpl_statistics_t rpl_statistics;
	rpl_statistics_delta_t rpl_statistics_delta;

	rpl_errors_t  rpl_errors;
	rpl_errors_delta_t rpl_errors_delta;
};

static uint16_t last_simple_id = 0;
static void node_update_old_field(const di_node_t *node, int field_offset, int field_size);

size_t node_sizeof() {
	return sizeof(di_node_t);
}

void node_init(void *data, const void *key, size_t key_size) {
	di_node_t *node = (di_node_t*) data;
	di_node_key_t node_key = {*(di_node_ref_t*)key};

	assert(key_size == sizeof(di_node_ref_t));

	node->dodag.version = -1;
	node->has_changed = true;
    node->has_errors = 0;

    init_sixlowpan_config(&node->sixlowpan_config);
    init_sixlowpan_statistics(&node->sixlowpan_statistics);
    init_sixlowpan_statistics_delta(&node->sixlowpan_statistics_delta);

    init_rpl_instance_config(&node->rpl_instance_config);
    init_rpl_instance_data(&node->rpl_instance_data);
    init_rpl_dodag_config(&node->rpl_dodag_config);
    init_rpl_prefix(&node->rpl_dodag_prefix_info);
    init_rpl_statistics(&node->rpl_statistics);
    init_rpl_errors(&node->rpl_errors);

	node->routes_delta = 0;

	last_simple_id++;
	node->simple_id = last_simple_id;

	node_set_key(node, &node_key);

	rpl_event_node(node, RET_Created);
}

void node_destroy(void *data) {
	di_node_t *node = (di_node_t*) data;

	route_destroy(&node->routes);
}

di_node_t *node_dup(di_node_t *node) {
	di_node_t *new_node;

	new_node = malloc(sizeof(di_node_t));
	memcpy(new_node, node, sizeof(di_node_t));
	new_node->routes = route_dup(&node->routes);

	//packet_count_delta is filled in by node_update_old_field
	init_sixlowpan_statistics_delta(&new_node->sixlowpan_statistics_delta);

	//routes_delta is not computed in fill_delta but on the fly
	node->routes_delta = false;

	return new_node;
}

void node_fill_delta(di_node_t *node, di_node_t const *prev_node) {
    sixlowpan_config_delta( node_get_sixlowpan_config(prev_node), node_get_sixlowpan_config(node), &node->sixlowpan_config_delta);
    //sixlowpan statistics are collected as packet as received
    //node->packet_count_delta = node->packet_count - prev_node->packet_count;

    rpl_instance_config_delta( node_get_instance_config(prev_node), node_get_instance_config(node), &node->rpl_instance_config_delta);
    rpl_instance_data_delta( node_get_instance_data(prev_node), node_get_instance_data(node), &node->rpl_instance_data_delta);
    rpl_dodag_config_delta( node_get_dodag_config(prev_node), node_get_dodag_config(node), &node->rpl_dodag_config_delta);
    rpl_prefix_delta( node_get_dodag_prefix_info(prev_node), node_get_dodag_prefix_info(node), &node->rpl_dodag_prefix_info_delta);
    rpl_statistics_delta(node_get_rpl_statistics(prev_node), node_get_rpl_statistics(node), &node->rpl_statistics_delta);

    rpl_errors_delta(node_get_rpl_errors(prev_node), node_get_rpl_errors(node), &node->rpl_errors_delta);
    node->has_errors = node->rpl_errors_delta.has_changed;
}

void node_set_changed(di_node_t *node) {
    if(node->has_changed == false)
        rpl_event_node(node, RET_Updated);
    node->has_changed = true;
}

static void node_update_old_field(const di_node_t *node, int field_offset, int field_size) {
    di_node_t **versionned_node_ptr;
    int version = rpldata_get_wsn_last_version();
    if(version) {
        hash_container_ptr container = rpldata_get_nodes(version);
        if(container) {
            versionned_node_ptr = (di_node_t**)hash_value(container, hash_key_make(node->key.ref), HVM_FailIfNonExistant, NULL);
            if(versionned_node_ptr && *versionned_node_ptr != node)
                memcpy((char*)(*versionned_node_ptr) + field_offset, (char*)node + field_offset, field_size);
        }
    }
}

bool node_has_changed(di_node_t *node) {
    return node->has_changed;
}

void node_reset_changed(di_node_t *node) {
    node->has_changed = false;
}

void node_key_init(di_node_key_t *key, addr_wpan_t wpan_address, uint32_t version) {
	memset(key, 0, sizeof(di_node_key_t));

	key->ref.wpan_address = wpan_address;
}

void node_ref_init(di_node_ref_t *ref, addr_wpan_t wpan_address) {
	memset(ref, 0, sizeof(di_node_ref_t));

	ref->wpan_address = wpan_address;
}

uint16_t node_get_simple_id(const di_node_t *node) {
    return node->simple_id;
}

void node_set_key(di_node_t *node, const di_node_key_t *key) {
	if(memcmp(&node->key, key, sizeof(di_node_key_t))) {
		node->key = *key;

		if(!node->sixlowpan_config.is_custom_local_address) {
			node->sixlowpan_config.local_address = addr_get_local_ip_from_mac64(node->key.ref.wpan_address);
		}

		node_set_changed(node);
	}
}

const di_node_key_t *node_get_key(const di_node_t *node) {
    return &node->key;
}

void node_set_dodag(di_node_t *node, const di_dodag_ref_t *dodag_ref) {
    if(memcmp(&node->dodag, dodag_ref, sizeof(di_dodag_ref_t))) {
        node->dodag = *dodag_ref;
        //New dodag version, ignore all previous info
        node->has_rpl_instance_config = false;
        node->rpl_instance_config.has_dodagid = false;
        node->rpl_instance_config.has_dio_config = false;
        node->has_rpl_instance_data = false;
        node->rpl_instance_data.has_dao_data = false;
        node->rpl_instance_data.has_dio_data = false;
        node->rpl_instance_data.has_rank = false;
        node->rpl_instance_data.has_metric = false;
        node->has_rpl_dodag_config = false;
        node->has_rpl_dodag_prefix_info = false;
        node_set_changed(node);
    }
}

const di_dodag_ref_t * node_get_dodag(const di_node_t *node) {
    if(node->dodag.version >= 0)
        return &node->dodag;

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// DATA SETTERS
///////////////////////////////////////////////////////////////////////////////

void node_set_local_ip(di_node_t *node, addr_ipv6_t address) {
    if(addr_compare_ip(&node->sixlowpan_config.local_address, &address)) {
        node->sixlowpan_config.local_address = address;
        node->sixlowpan_config.is_custom_local_address = true;
        node_set_changed(node);
    }
}

void node_set_global_ip(di_node_t *node, addr_ipv6_t address) {
    if(addr_compare_ip(&node->sixlowpan_config.global_address, &address)) {
        node->sixlowpan_config.global_address = address;
        node->sixlowpan_config.is_custom_global_address = true;
        node_set_changed(node);
    }
}

void node_update_ip(di_node_t *node, const di_prefix_t *prefix) {
    if(!node->sixlowpan_config.is_custom_global_address) {
        addr_ipv6_t ip = addr_get_global_ip_from_mac64(*prefix, node->key.ref.wpan_address);
        if(memcmp(&ip, &node->sixlowpan_config.global_address, sizeof(addr_ipv6_t))) {
            node->sixlowpan_config.global_address = ip;
            node_set_changed(node);
        }
    }
}

void node_update_from_dio(di_node_t *node, const rpl_dio_t *dio, const di_dodag_t *dodag) {
    if ( ! dio ) return;
    rpl_instance_config_t new_config = node->rpl_instance_config;
    rpl_instance_data_t new_data = node->rpl_instance_data;
    update_rpl_instance_config_from_dio(&new_config, dio);
    update_rpl_instance_data_from_dio(&new_data, dio);
    if( rpl_instance_config_compare(&new_config, &node->rpl_instance_config)) {
        node->rpl_instance_config = new_config;
        node_set_changed(node);
    }
    if(rpl_instance_data_compare(&new_data, &node->rpl_instance_data)) {
        node->rpl_instance_data = new_data;
        node_set_changed(node);
    }
    //Do not check instance config, instance config is the dodag reference and not actual config
    /*
    if ( dodag ) {
        rpl_instance_config_t const *instance_config = dodag_get_instance_config(dodag);
        rpl_instance_config_delta(instance_config, &node->rpl_instance_config, &node->actual_rpl_instance_config_delta);
        if ( node->actual_rpl_instance_config_delta.has_changed ) {
            node_add_dodag_config_mismatch_error(node);
        }
    }*/
    node->rpl_statistics.dio++;

    node->has_rpl_instance_config = true;
    node->has_rpl_instance_data = true;
}

void node_update_from_metric(di_node_t *node, const rpl_metric_t *metric) {
    rpl_instance_data_t new_data = node->rpl_instance_data;
    update_rpl_instance_data_from_metric(&new_data, metric);
    if ( rpl_instance_data_compare(&new_data, &node->rpl_instance_data) )  {
        node->rpl_instance_data = new_data;
        node->has_rpl_instance_data = true;
        node_set_changed(node);
    }
}

void node_update_from_hop_by_hop(di_node_t *node, const rpl_hop_by_hop_opt_t * hop_by_hop) {
    if ( ! hop_by_hop ) return;
    rpl_instance_data_t new_data = node->rpl_instance_data;
    update_rpl_instance_data_from_hop_by_hop(&new_data, hop_by_hop);
    if ( rpl_instance_data_compare(&new_data, &node->rpl_instance_data) )  {
        node->rpl_instance_data = new_data;
        node->has_rpl_instance_data = true;
        node_set_changed(node);
    }
}

void node_update_from_dao(di_node_t *node, const rpl_dao_t * dao, const di_dodag_t *dodag) {
    if ( ! dao ) return;
    rpl_instance_config_t new_config = node->rpl_instance_config;
    rpl_instance_data_t new_data = node->rpl_instance_data;
    update_rpl_instance_config_from_dao(&new_config, dao);
    update_rpl_instance_data_from_dao(&new_data, dao);
    if( rpl_instance_config_compare(&new_config, &node->rpl_instance_config)) {
        node->rpl_instance_config = new_config;
        node->has_rpl_instance_config = true;
        node_set_changed(node);
    }
    if ( rpl_instance_data_compare(&new_data, &node->rpl_instance_data) )  {
        node->rpl_instance_data = new_data;
        node->has_rpl_instance_data = true;
        node_set_changed(node);
    }
    //Do not check instance config, instance config is the dodag reference and not actual config
    /*
    if ( dodag ) {
        rpl_instance_config_t const *instance_config = dodag_get_instance_config(dodag);
        rpl_instance_config_delta(instance_config, &node->rpl_instance_config, &node->actual_rpl_instance_config_delta);
        if ( node->actual_rpl_instance_config_delta.has_changed ) {
            node_add_dodag_config_mismatch_error(node);
        }
    }*/
    node->rpl_statistics.dao++;
    node_set_changed(node);
}

void node_update_from_dodag_config(di_node_t *node, const rpl_dodag_config_t *config, const di_dodag_t *dodag) {
    if ( config ) {
        if( rpl_dodag_config_compare(config, &node->rpl_dodag_config) ) {
            node->rpl_dodag_config = *config;
            node_set_changed(node);
        }
        node->has_rpl_dodag_config = true;
    } else {
        if ( node->has_rpl_dodag_config ) {
            node_set_changed(node);
        }
        node->has_rpl_dodag_config = false;
    }
    if ( dodag ) {
        rpl_dodag_config_t const *dodag_config = dodag_get_dodag_config(dodag);
        if ( dodag_config ) {
            rpl_dodag_config_delta(dodag_config, &node->rpl_dodag_config, &node->actual_rpl_dodag_config_delta);
            if ( node->actual_rpl_dodag_config_delta.has_changed ) {
                node_add_dodag_config_mismatch_error(node);
            }
        }
    }
}

void node_update_from_dodag_prefix_info(di_node_t *node, const rpl_prefix_t *prefix_info, const di_dodag_t *dodag) {
    if ( prefix_info ) {
        if( rpl_prefix_compare(prefix_info, &node->rpl_dodag_prefix_info) ) {
            node->rpl_dodag_prefix_info = *prefix_info;
            node_update_ip(node, &prefix_info->prefix);
            node_set_changed(node);
        }
        node->has_rpl_dodag_prefix_info = true;
    } else {
        if ( node->has_rpl_dodag_prefix_info ) {
            node_set_changed(node);
        }
        node->has_rpl_dodag_prefix_info = false;
    }
    if ( dodag ) {
        rpl_prefix_t const *dodag_prefix_info = dodag_get_prefix(dodag);
        if ( dodag_prefix_info ) {
            rpl_prefix_delta( dodag_prefix_info, &node->rpl_dodag_prefix_info, &node->actual_rpl_dodag_prefix_info_delta);
            if ( node->actual_rpl_dodag_prefix_info_delta.has_changed) {
                node_add_dodag_config_mismatch_error(node);
            }
        }
    }
}

void node_update_from_dis(di_node_t *node, const rpl_dis_opt_info_req_t *dis) {
    node->rpl_statistics.dis++;
    node_set_changed(node);
}

void node_update_from_dodag(di_node_t *node, const di_dodag_t *dodag) {
    if ( !dodag) return;
    rpl_instance_config_t const * instance_config = NULL;
    rpl_dodag_config_t const * dodag_config = NULL;
    rpl_prefix_t const * dodag_prefix_info = NULL;
    /*
    di_dodag_ref_t const * dodag_ref = node_get_dodag(node);
    if (dodag_ref) {
        di_dodag_t const * dodag = rpldata_get_dodag(dodag_ref, HVM_FailIfNonExistant, NULL);
        if ( dodag ) {
            instance_config = dodag_get_instance_config(dodag);
            dodag_config = dodag_get_dodag_config(dodag);
            dodag_prefix_info = dodag_get_prefix(dodag);
        }
    }
    */
    instance_config = dodag_get_instance_config(dodag);
    dodag_config = dodag_get_dodag_config(dodag);
    dodag_prefix_info = dodag_get_prefix(dodag);
    rpl_instance_config_delta(instance_config, &node->rpl_instance_config, &node->actual_rpl_instance_config_delta);
    rpl_dodag_config_delta(dodag_config, &node->rpl_dodag_config, &node->actual_rpl_dodag_config_delta);
    rpl_prefix_delta( dodag_prefix_info, &node->rpl_dodag_prefix_info, &node->actual_rpl_dodag_prefix_info_delta);

    if (node->actual_rpl_instance_config_delta.has_changed ||
        node->actual_rpl_dodag_config_delta.has_changed ||
        node->actual_rpl_dodag_prefix_info_delta.has_changed) {
      node_add_dodag_config_mismatch_error(node);
    }
}

void node_add_route(di_node_t *node, const di_route_t *route_prefix, addr_wpan_t via_node) {
	bool route_already_existing = false;
	route_add(&node->routes, *route_prefix, via_node, false, &route_already_existing);

	if(route_already_existing == false) {
	    node->routes_delta = true;
		node_set_changed(node);
	}
}

void node_del_route(di_node_t *node, const di_route_t *route_prefix, addr_wpan_t via_node) {
	if(route_remove(&node->routes, *route_prefix, via_node)) {
	    node->routes_delta = true;
		node_set_changed(node);
	}
}

void node_add_packet_count(di_node_t *node, int count) {
	node->sixlowpan_statistics.packet_count += count;
    node->sixlowpan_statistics_delta.packet_count += count;

	node_update_old_field(node, offsetof(di_node_t, sixlowpan_statistics.packet_count), sizeof(node->sixlowpan_statistics.packet_count));
    node_update_old_field(node, offsetof(di_node_t, sixlowpan_statistics_delta.packet_count), sizeof(node->sixlowpan_statistics_delta.packet_count));
}

void node_update_dao_interval(di_node_t *node, double timestamp) {

	if(node->rpl_statistics.last_dao_timestamp) {
		double interval = timestamp - node->rpl_statistics.last_dao_timestamp;
		if(!node->rpl_statistics.max_dao_interval || (node->rpl_statistics.max_dao_interval < interval)) {
			node->rpl_statistics.max_dao_interval = interval;
			//node_update_old_field(node, offsetof(di_node_t, max_dao_interval), sizeof(node->max_dao_interval));
		}
	}

	node->rpl_statistics.last_dao_timestamp = timestamp;
    node_set_changed(node);
}

void node_update_dio_interval(di_node_t *node, double timestamp) {
	if(node->rpl_statistics.last_dio_timestamp) {
		double interval = timestamp - node->rpl_statistics.last_dio_timestamp;
		if(!node->rpl_statistics.max_dio_interval || (node->rpl_statistics.max_dio_interval < interval)) {
			node->rpl_statistics.max_dio_interval = interval;
			//node_update_old_field(node, offsetof(di_node_t, max_dio_interval), sizeof(node->max_dio_interval));
		}
	}

	node->rpl_statistics.last_dio_timestamp = timestamp;
    node_set_changed(node);
}

void node_add_upward_error(di_node_t *node) {
	node->rpl_errors.upward_rank_errors++;
	node_set_changed(node);
}

void node_add_downward_error(di_node_t *node) {
	node->rpl_errors.downward_rank_errors++;
	node_set_changed(node);
}

void node_add_route_error(di_node_t *node) {
	node->rpl_errors.route_loop_errors++;
	node_set_changed(node);
}

void node_add_dodag_version_error(di_node_t *node) {
	node->rpl_errors.dodag_version_decrease_errors++;
	node_set_changed(node);
}

void node_add_ip_mismatch_error(di_node_t *node) {
	node->rpl_errors.ip_mismatch_errors++;
	node_set_changed(node);
}

void node_add_dodag_mismatch_error(di_node_t *node) {
	node->rpl_errors.dodag_mismatch_errors++;
	node_set_changed(node);
}

void node_add_dodag_config_mismatch_error(di_node_t *node) {
    node->rpl_errors.dodag_config_mismatch_errors++;
    node_set_changed(node);
}

///////////////////////////////////////////////////////////////////////////////
// DAT GETTERS
///////////////////////////////////////////////////////////////////////////////

addr_wpan_t node_get_mac64(const di_node_t *node) {
	return node->key.ref.wpan_address;
}

const sixlowpan_config_t *node_get_sixlowpan_config(const di_node_t *node) {
    if (!node) return NULL;
    return &node->sixlowpan_config;
}

const sixlowpan_config_delta_t *node_get_sixlowpan_config_delta(const di_node_t *node) {
    return &node->sixlowpan_config_delta;
}

const addr_ipv6_t* node_get_local_ip(const di_node_t *node) {
    return &node->sixlowpan_config.local_address;
}

const addr_ipv6_t* node_get_global_ip(const di_node_t *node) {
    return &node->sixlowpan_config.global_address;
}

const rpl_instance_config_t *node_get_instance_config(const di_node_t *node) {
    if (!node) return NULL;
    if ( node->has_rpl_instance_config ) {
        return &node->rpl_instance_config;
    } else {
        return NULL;
    }
}

const rpl_instance_config_delta_t *node_get_instance_config_delta(const di_node_t *node) {
    return &node->rpl_instance_config_delta;
}

const rpl_instance_config_delta_t *node_get_actual_instance_config_delta(const di_node_t *node) {
    return &node->actual_rpl_instance_config_delta;
}

const rpl_instance_data_t *node_get_instance_data(const di_node_t *node) {
    if (!node) return NULL;
    if ( node->has_rpl_instance_data ) {
        return &node->rpl_instance_data;
    } else {
        return NULL;
    }
}

const rpl_instance_data_delta_t *node_get_instance_data_delta(const di_node_t *node) {
    return &node->rpl_instance_data_delta;
}

int node_get_rank(const di_node_t *node) {
    return node->rpl_instance_data.rank;
}

const rpl_dodag_config_t *node_get_dodag_config(const di_node_t *node) {
    if (!node) return NULL;
    if ( node-> has_rpl_dodag_config ) {
        return &node->rpl_dodag_config;
    } else {
        return NULL;
    }
}

const rpl_dodag_config_delta_t *node_get_dodag_config_delta(const di_node_t *node) {
    return &node->rpl_dodag_config_delta;
}

const rpl_dodag_config_delta_t *node_get_actual_dodag_config_delta(const di_node_t *node) {
    return &node->actual_rpl_dodag_config_delta;
}

const rpl_prefix_t *node_get_dodag_prefix_info(const di_node_t *node) {
    if (!node) return NULL;
    if ( node->has_rpl_dodag_prefix_info ) {
        return &node->rpl_dodag_prefix_info;
    } else {
      return NULL;
    }
}

const rpl_prefix_delta_t *node_get_dodag_prefix_info_delta(const di_node_t *node) {
    return &node->rpl_dodag_prefix_info_delta;
}

const rpl_prefix_delta_t *node_get_actual_dodag_prefix_info_delta(const di_node_t *node) {
    return &node->actual_rpl_dodag_prefix_info_delta;
}

di_route_list_t node_get_routes(const di_node_t *node) {
	return node->routes;
}

const sixlowpan_statistics_t *node_get_sixlowpan_statistics(const di_node_t *node){
    return &node->sixlowpan_statistics;
}

const sixlowpan_statistics_delta_t *node_get_sixlowpan_statistics_delta(const di_node_t *node) {
    return &node->sixlowpan_statistics_delta;
}

const rpl_statistics_t *node_get_rpl_statistics(const di_node_t *node) {
    if (!node) return NULL;
    return &node->rpl_statistics;
}
const rpl_statistics_delta_t *node_get_rpl_statistics_delta(const di_node_t *node) {
    return &node->rpl_statistics_delta;
}

const rpl_errors_t *node_get_rpl_errors(const di_node_t *node) {
    if (!node) return NULL;
    return &node->rpl_errors;
}

const rpl_errors_delta_t *node_get_rpl_errors_delta(const di_node_t *node) {
    return &node->rpl_errors_delta;
}

bool node_get_routes_delta(const di_node_t *node) {
    return node->routes_delta;
}

int node_get_has_errors(const di_node_t *node) {
    return node->has_errors;
}
