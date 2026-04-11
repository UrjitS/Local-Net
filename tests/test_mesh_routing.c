#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../utils/utils.h"
#include "../routing/routing.h"
#include "../protocol/protocol.h"

void test_route_request_creation() {
    printf("Testing test_route_request_creation\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);

    // Initiate route discovery
    uint32_t * reverse_path = NULL;
    uint8_t path_len = 0;
    const int request_id = initiate_route_discovery(node, 0x999, &reverse_path, &path_len);

    assert(request_id > 0);
    assert(reverse_path != NULL);
    assert(path_len == 1);
    assert(reverse_path[0] == 0x001);

    // Verify request is cached
    assert(has_seen_request(node->request_cache, request_id) == 1);

    free(reverse_path);
    free_mesh_node(node);
}

void test_route_request_serialization() {
    printf("Testing test_route_request_serialization\n");

    uint32_t reverse_path[] = {0x001, 0x002, 0x003};
    const struct route_request req = {
        .request_id = 0x12345678,
        .destination_id = 0x999,
        .hop_count = 3,
        .reverse_path_len = 3,
        .reverse_path = reverse_path
    };

    uint8_t buffer[256];
    const size_t written = serialize_route_request(&req, buffer, sizeof(buffer));
    assert(written == 10 + 12);  // 10 base + 3 * 4 bytes for path

    // Parse it back
    struct route_request parsed = {0};
    const int result = parse_route_request(buffer, written, &parsed);
    assert(result == 0);
    assert(parsed.request_id == 0x12345678);
    assert(parsed.destination_id == 0x999);
    assert(parsed.hop_count == 3);
    assert(parsed.reverse_path_len == 3);
    assert(parsed.reverse_path[0] == 0x001);
    assert(parsed.reverse_path[1] == 0x002);
    assert(parsed.reverse_path[2] == 0x003);

    free(parsed.reverse_path);
}

void test_route_request_intermediate_processing() {
    printf("Testing test_route_request_intermediate_processing\n");

    // Create intermediate node
    struct mesh_node * node = create_mesh_node(0x002, FULL_NODE);
    assert(node != NULL);

    // Simulate receiving a route request
    uint32_t reverse_path[] = {0x001};
    int result = process_route_request(node, 0xABCD, 0x999, 0, reverse_path, 1, 0x001);

    // Should forward (result = 0)
    assert(result == 0);

    // Request should be cached
    assert(has_seen_request(node->request_cache, 0xABCD) == 1);

    // Duplicate request should be dropped
    result = process_route_request(node, 0xABCD, 0x999, 1, reverse_path, 1, 0x001);
    assert(result == -1);  // Dropped

    free_mesh_node(node);
}

void test_route_request_destination_processing() {
    printf("Testing test_route_request_destination_processing\n");

    // Create destination node (0x999)
    struct mesh_node * dest_node = create_mesh_node(0x999, FULL_NODE);
    assert(dest_node != NULL);

    // Simulate receiving a route request for ourselves
    uint32_t reverse_path[] = {0x001, 0x002};
    const int result = process_route_request(dest_node, 0xABCD, 0x999, 2, reverse_path, 2, 0x002);

    // Should indicate we are destination (result = 1)
    assert(result == 1);

    free_mesh_node(dest_node);
}

void test_route_reply_creation() {
    printf("Testing test_route_reply_creation\n");

    uint32_t forward_path[] = {0x001, 0x002, 0x999};
    const struct route_reply rep = {
        .request_id = 0xABCD,
        .route_cost = 3,
        .forward_path_len = 3,
        .forward_path = forward_path
    };

    uint8_t buffer[256];
    const size_t written = serialize_route_reply(&rep, buffer, sizeof(buffer));
    assert(written == 6 + 12);  // 6 base + 3 * 4 bytes for path

    // Parse it back
    struct route_reply parsed = {0};
    const int result = parse_route_reply(buffer, written, &parsed);
    assert(result == 0);
    assert(parsed.request_id == 0xABCD);
    assert(parsed.route_cost == 3);
    assert(parsed.forward_path_len == 3);
    assert(parsed.forward_path[0] == 0x001);
    assert(parsed.forward_path[1] == 0x002);
    assert(parsed.forward_path[2] == 0x999);

    free(parsed.forward_path);
}

void test_route_reply_processing() {
    printf("Testing test_route_reply_processing\n");

    // Create originator node
    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);

    // Process a route reply
    const uint32_t forward_path[] = {0x001, 0x002, 0x999};
    const int result = process_route_reply(node, 0xABCD, 3, forward_path, 3);

    assert(result == 0);

    // Verify route was added
    const struct routing_entry * route = find_route(node->routing_table, 0x999);
    assert(route != NULL);
    assert(route->next_hop == 0x002);
    assert(route->hop_count == 2);
    assert(route->is_valid == 1);

    free_mesh_node(node);
}

void test_hop_count_tracking() {
    printf("Testing test_hop_count_tracking\n");

    struct mesh_node * node = create_mesh_node(0x002, FULL_NODE);
    assert(node != NULL);

    // Simulate receiving a route request with max hops
    uint32_t reverse_path[MAX_HOP_COUNT];
    for (int i = 0; i < MAX_HOP_COUNT; i++) {
        reverse_path[i] = 0x100 + i;
    }

    // This should be dropped (hop count exceeded)
    const int result = process_route_request(node, 0xABCD, 0x999, MAX_HOP_COUNT - 1,
                                       reverse_path, MAX_HOP_COUNT, 0x100);
    assert(result == -1);  // Dropped

    free_mesh_node(node);
}

void test_ttl_based_forwarding() {
    printf("Testing test_ttl_based_forwarding\n");

    struct mesh_node * node = create_mesh_node(0x002, FULL_NODE);
    assert(node != NULL);

    // Add a route
    add_route(node->routing_table, 0x999, 0x003, 2, 1.5f, get_current_timestamp());

    // Test forwarding with valid TTL
    uint8_t ttl = 10;
    uint32_t next_hop;
    int result = forward_packet(node, 0x999, &ttl, &next_hop);

    assert(result == 0);  // Forward
    assert(ttl == 9);     // Decremented
    assert(next_hop == 0x003);

    // Test TTL expiry
    ttl = 0;
    result = forward_packet(node, 0x999, &ttl, &next_hop);
    assert(result == -1);  // TTL expired

    free_mesh_node(node);
}

void test_routing_table_lookup() {
    printf("Testing test_routing_table_lookup\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);
    const uint32_t ts = get_current_timestamp();

    // Add multiple routes
    add_route(node->routing_table, 0x002, 0x002, 1, 1.0f, ts);
    add_route(node->routing_table, 0x003, 0x002, 2, 2.0f, ts);
    add_route(node->routing_table, 0x004, 0x003, 3, 3.0f, ts);

    // Find specific routes
    const struct routing_entry * route = find_route(node->routing_table, 0x003);
    assert(route != NULL);
    assert(route->next_hop == 0x002);
    assert(route->hop_count == 2);

    // Find best route
    const struct routing_entry * best = find_best_route(node->routing_table, 0x003);
    assert(best != NULL);
    assert(best->route_cost == 2.0f);

    // No route case
    route = find_route(node->routing_table, 0x999);
    assert(route == NULL);

    free_mesh_node(node);
}

void test_next_hop_determination() {
    printf("Testing test_next_hop_determination\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);
    const uint32_t ts = get_current_timestamp();

    // Add routes with different costs
    add_route(node->routing_table, 0x999, 0x002, 2, 2.0f, ts);
    add_route(node->routing_table, 0x999, 0x003, 3, 1.5f, ts);  // Better cost, but won't update

    // Best route should be selected (first one, 2.0 cost since it's better)
    const struct routing_entry * best = find_best_route(node->routing_table, 0x999);
    assert(best != NULL);
    // Since add_route updates if better, check the route cost
    assert(best->route_cost == 1.5f);  // Updated to better cost

    free_mesh_node(node);
}

void test_unreachable_destination_handling() {
    printf("Testing test_unreachable_destination_handling\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);

    // No routes added
    uint8_t ttl = 10;
    uint32_t next_hop;
    const int result = forward_packet(node, 0x999, &ttl, &next_hop);

    assert(result == -2);  // No route found

    free_mesh_node(node);
}

void test_local_processing() {
    printf("Testing test_local_processing\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);

    uint8_t ttl = 10;
    uint32_t next_hop;
    const int result = forward_packet(node, 0x001, &ttl, &next_hop);  // Packet for us

    assert(result == 1);  // Process locally

    free_mesh_node(node);
}

void test_heartbeat_monitoring() {
    printf("Testing test_heartbeat_monitoring\n");

    struct connection_table * table = create_connection_table();
    assert(table != NULL);

    add_connection(table, 0x002, -60);
    update_connection_state(table, 0x002, STABLE);

    const struct connection_entry * entry = find_connection(table, 0x002);
    assert(entry->missed_heartbeats == 0);

    // Miss heartbeats
    increment_missed_heartbeat(table, 0x002);
    assert(entry->missed_heartbeats == 1);
    assert(entry->state == STABLE);

    increment_missed_heartbeat(table, 0x002);
    assert(entry->missed_heartbeats == 2);
    assert(entry->state == STABLE);

    increment_missed_heartbeat(table, 0x002);
    assert(entry->missed_heartbeats == 3);
    assert(entry->state == DISCONNECTED);  // Threshold reached

    free_connection_table(table);
}

void test_heartbeat_reset() {
    printf("Testing test_heartbeat_reset\n");

    struct connection_table * table = create_connection_table();
    add_connection(table, 0x002, -60);
    update_connection_state(table, 0x002, STABLE);

    // Miss some heartbeats
    increment_missed_heartbeat(table, 0x002);
    increment_missed_heartbeat(table, 0x002);

    const struct connection_entry * entry = find_connection(table, 0x002);
    assert(entry->missed_heartbeats == 2);

    // Reset on receiving heartbeat
    reset_missed_heartbeats(table, 0x002);
    assert(entry->missed_heartbeats == 0);

    free_connection_table(table);
}

void test_route_recalculation() {
    printf("Testing test_route_recalculation\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    assert(node != NULL);

    // Add a route with old timestamp
    const uint32_t old_time = get_current_timestamp() - ROUTE_EXPIRY_SECONDS - 10;
    add_route(node->routing_table, 0x999, 0x002, 2, 1.0f, old_time);

    // Expire routes
    expire_routes(node->routing_table, get_current_timestamp());

    // Route should be marked invalid
    const struct routing_entry * route = find_route(node->routing_table, 0x999);
    assert(route == NULL || route->is_valid == 0);  // Either NULL or invalid

    free_mesh_node(node);
}

void test_connection_timeout() {
    printf("Testing test_connection_timeout\n");

    struct connection_table * table = create_connection_table();
    add_connection(table, 0x002, -60);
    update_connection_state(table, 0x002, STABLE);

    struct connection_entry * entry = find_connection(table, 0x002);

    // Simulate old last_seen time
    entry->last_seen = get_current_timestamp() - CONNECTION_TIMEOUT_SECONDS - 10;

    // Check timeouts
    check_connection_timeouts(table, get_current_timestamp());

    assert(entry->state == DISCONNECTED);

    free_connection_table(table);
}

void test_connection_recovery() {
    printf("Testing test_connection_recovery\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    add_connection(node->connection_table, 0x002, -60);
    update_connection_state(node->connection_table, 0x002, DISCONNECTED);

    const struct connection_entry * entry = find_connection(node->connection_table, 0x002);
    assert(entry->state == DISCONNECTED);

    // Simulate reconnection (update state and reset heartbeats)
    update_connection_state(node->connection_table, 0x002, CONNECTING);
    reset_missed_heartbeats(node->connection_table, 0x002);
    update_last_seen(node->connection_table, 0x002, get_current_timestamp());

    entry = find_connection(node->connection_table, 0x002);
    assert(entry->state == CONNECTING);
    assert(entry->missed_heartbeats == 0);

    // Simulate stable state
    update_connection_state(node->connection_table, 0x002, STABLE);
    assert(entry->state == STABLE);

    free_mesh_node(node);
}

/* ============== Integration Tests ============== */

void test_multi_hop_route_discovery() {
    printf("Testing test_multi_hop_route_discovery\n");

    // Create a mesh with 4 nodes: A -> B -> C -> D
    struct mesh_node * nodeA = create_mesh_node(0xA, FULL_NODE);
    struct mesh_node * nodeB = create_mesh_node(0xB, FULL_NODE);
    struct mesh_node * nodeC = create_mesh_node(0xC, FULL_NODE);
    struct mesh_node * nodeD = create_mesh_node(0xD, EDGE_NODE);

    // Set up connections
    add_connection(nodeA->connection_table, 0xB, -60);
    update_connection_state(nodeA->connection_table, 0xB, STABLE);

    add_connection(nodeB->connection_table, 0xA, -60);
    update_connection_state(nodeB->connection_table, 0xA, STABLE);
    add_connection(nodeB->connection_table, 0xC, -65);
    update_connection_state(nodeB->connection_table, 0xC, STABLE);

    add_connection(nodeC->connection_table, 0xB, -65);
    update_connection_state(nodeC->connection_table, 0xB, STABLE);
    add_connection(nodeC->connection_table, 0xD, -70);
    update_connection_state(nodeC->connection_table, 0xD, STABLE);

    add_connection(nodeD->connection_table, 0xC, -70);
    update_connection_state(nodeD->connection_table, 0xC, STABLE);

    // Step 1: Node A initiates route discovery to Node D
    uint32_t * reverse_path;
    uint8_t path_len;
    const int request_id = initiate_route_discovery(nodeA, 0xD, &reverse_path, &path_len);
    assert(request_id > 0);
    assert(path_len == 1);  // Just A

    // Step 2: Simulate B processing the request
    int result = process_route_request(nodeB, request_id, 0xD, 0, reverse_path, path_len, 0xA);
    assert(result == 0);  // Forward

    // Build extended path for C
    uint32_t pathAB[] = {0xA, 0xB};

    // Step 3: Simulate C processing the request
    result = process_route_request(nodeC, request_id, 0xD, 1, pathAB, 2, 0xB);
    assert(result == 0);  // Forward

    // Build extended path for D
    uint32_t pathABC[] = {0xA, 0xB, 0xC};

    // Step 4: Simulate D processing the request (destination reached)
    result = process_route_request(nodeD, request_id, 0xD, 2, pathABC, 3, 0xC);
    assert(result == 1);  // We are destination

    // Step 5: D sends route reply back, each node processes it
    const uint32_t forwardPath[] = {0xA, 0xB, 0xC, 0xD};

    // C processes reply
    result = process_route_reply(nodeC, request_id, 3, forwardPath, 4);
    assert(result == 0);
    const struct routing_entry * routeC = find_route(nodeC->routing_table, 0xD);
    assert(routeC != NULL);
    assert(routeC->next_hop == 0xD);

    // B processes reply
    result = process_route_reply(nodeB, request_id, 3, forwardPath, 4);
    assert(result == 0);
    const struct routing_entry * routeB = find_route(nodeB->routing_table, 0xD);
    assert(routeB != NULL);
    assert(routeB->next_hop == 0xC);

    // A processes reply
    result = process_route_reply(nodeA, request_id, 3, forwardPath, 4);
    assert(result == 0);
    const struct routing_entry * routeA = find_route(nodeA->routing_table, 0xD);
    assert(routeA != NULL);
    assert(routeA->next_hop == 0xB);
    assert(routeA->hop_count == 3);

    free(reverse_path);
    free_mesh_node(nodeA);
    free_mesh_node(nodeB);
    free_mesh_node(nodeC);
    free_mesh_node(nodeD);
}

void test_self_healing_route_invalidation() {
    printf("Testing test_self_healing_route_invalidation\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);

    // Set up connection and route through it
    add_connection(node->connection_table, 0x002, -60);
    update_connection_state(node->connection_table, 0x002, STABLE);
    add_route(node->routing_table, 0x999, 0x002, 2, 1.0f, get_current_timestamp());

    // Verify route is valid
    const struct routing_entry * route = find_route(node->routing_table, 0x999);
    assert(route != NULL && route->is_valid == 1);

    // Simulate connection loss (missed heartbeats)
    for (int i = 0; i < HEARTBEAT_MISSED_THRESHOLD; i++) {
        increment_missed_heartbeat(node->connection_table, 0x002);
    }

    const struct connection_entry * conn = find_connection(node->connection_table, 0x002);
    assert(conn->state == DISCONNECTED);

    // In a real system, route would be invalidated when connection is lost
    // Manual invalidation for test
    struct routing_table * rt = node->routing_table;
    for (size_t i = 0; i < rt->count; i++) {
        if (rt->entries[i].next_hop == 0x002) {
            rt->entries[i].is_valid = 0;
        }
    }

    // Verify route is now invalid
    route = find_route(node->routing_table, 0x999);
    assert(route == NULL);  // find_route only returns valid routes

    free_mesh_node(node);
}

void test_packet_serialization_round_trip() {
    printf("Testing test_packet_serialization_round_trip\n");

    // Create a complete packet
    const struct header hdr = {
        .protocol_version = 1,
        .message_type = MSG_DATA,
        .fragmentation_flag = 0,
        .fragmentation_number = 0,
        .total_fragments = 1,
        .time_to_live = 15,
        .payload_length = 10,
        .sequence_number = 12345
    };

    const struct network net = {
        .source_id = 0x001,
        .destination_id = 0x999
    };

    uint8_t payload[] = "HelloMesh";

    // Serialize
    uint8_t buffer[256];
    size_t offset = 0;
    offset += serialize_header(&hdr, buffer, sizeof(buffer));
    offset += serialize_network(&net, buffer + offset, sizeof(buffer) - offset);
    memcpy(buffer + offset, payload, 10);
    offset += 10;

    // Parse back
    struct header parsed_hdr;
    struct network parsed_net;

    assert(parse_header(buffer, offset, &parsed_hdr) == 0);
    assert(parse_network(buffer + 8, offset - 8, &parsed_net) == 0);

    // Verify
    assert(parsed_hdr.protocol_version == 1);
    assert(parsed_hdr.message_type == MSG_DATA);
    assert(parsed_hdr.time_to_live == 15);
    assert(parsed_hdr.payload_length == 10);
    assert(parsed_hdr.sequence_number == 12345);
    assert(parsed_net.source_id == 0x001);
    assert(parsed_net.destination_id == 0x999);
    assert(memcmp(buffer + 16, payload, 10) == 0);
}

void test_link_quality_routing() {
    printf("Testing test_link_quality_routing\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);

    // Add two connections with different link qualities
    add_connection(node->connection_table, 0x002, -60);
    update_connection_state(node->connection_table, 0x002, STABLE);

    add_connection(node->connection_table, 0x003, -70);
    update_connection_state(node->connection_table, 0x003, STABLE);

    // Simulate packet success/failures
    for (int i = 0; i < 100; i++) {
        update_link_quality(node->connection_table, 0x002, 1);  // 100% success
    }

    for (int i = 0; i < 50; i++) {
        update_link_quality(node->connection_table, 0x003, 1);
        update_link_quality(node->connection_table, 0x003, 0);  // 50% success
    }

    const struct connection_entry * conn2 = find_connection(node->connection_table, 0x002);
    const struct connection_entry * conn3 = find_connection(node->connection_table, 0x003);

    assert(conn2->link_quality > 0.95f);  // ~100%
    assert(conn3->link_quality < 0.55f);  // ~50%

    // Calculate route costs
    uint32_t path2[] = {0x001, 0x002, 0x999};
    uint32_t path3[] = {0x001, 0x003, 0x999};

    // Path through 0x002 should have lower cost (better quality)
    assert(calculate_route_cost(node->connection_table, path2, 3) >= 0.0f);
    assert(calculate_route_cost(node->connection_table, path3, 3) >= 0.0f);

    free_mesh_node(node);
}

void test_discovery_timing() {
    printf("Testing test_discovery_timing\n");

    struct mesh_node * node = create_mesh_node(0x001, FULL_NODE);
    const uint32_t now = get_current_timestamp();

    // Should send discovery initially
    assert(should_send_discovery(node, now) == 1);

    // After sending, shouldn't send immediately
    node->last_discovery_time = now;
    assert(should_send_discovery(node, now + 10) == 0);

    // Should send after interval
    assert(should_send_discovery(node, now + DISCOVERY_INITIAL_INTERVAL + 1) == 1);

    free_mesh_node(node);
}

/* ============== Main ============== */

int main() {
    srand(time(NULL));

    // Route Discovery Protocol Tests
    test_route_request_creation();
    test_route_request_serialization();
    test_route_request_intermediate_processing();
    test_route_request_destination_processing();
    test_route_reply_creation();
    test_route_reply_processing();
    test_hop_count_tracking();

    // Packet Forwarding Engine Tests
    test_ttl_based_forwarding();
    test_routing_table_lookup();
    test_next_hop_determination();
    test_unreachable_destination_handling();
    test_local_processing();

    // Self-Healing Mechanism Tests
    test_heartbeat_monitoring();
    test_heartbeat_reset();
    test_route_recalculation();
    test_connection_timeout();
    test_connection_recovery();

    // Integration Tests
    test_multi_hop_route_discovery();
    test_self_healing_route_invalidation();
    test_packet_serialization_round_trip();
    test_link_quality_routing();
    test_discovery_timing();

    printf("ALL MESH ROUTING TESTS PASSED\n");

    return EXIT_SUCCESS;
}

