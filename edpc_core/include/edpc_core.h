#ifndef EDPC_CORE_H
#define EDPC_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EdpcModelHandle EdpcModelHandle;

typedef struct {
    float x;
    float y;
    float z;
} EdpcVec3;

typedef struct {
    float u;
    float v;
} EdpcVec2;

typedef struct {
    EdpcVec3 pos;
    EdpcVec3 normal;
    EdpcVec2 uv;
    uint32_t part_id;
} EdpcVertex;

typedef struct {
    EdpcVec3 base;
    EdpcVec3 elbow;
    EdpcVec3 tip;
    float shoe_offset;
    bool has_p4;
    EdpcVec3 p4_base;
    EdpcVec3 p4_elbow;
} EdpcPivots;

typedef struct {
    float angle1;
    float angle2;
    float angle4;
    bool reachable;
} EdpcAngles;

// Model loading and manipulation
EdpcModelHandle* edpc_model_load_file(const char* path);
EdpcModelHandle* edpc_model_load_string(const char* content);
void edpc_model_free(EdpcModelHandle* handle);

bool edpc_model_get_pivots(const EdpcModelHandle* handle, EdpcPivots* out_pivots);
bool edpc_model_solve_kinematics(const EdpcModelHandle* handle, float current_height, EdpcAngles* out_angles);
bool edpc_model_update_transformed_mesh(EdpcModelHandle* handle, float current_height, const EdpcVertex** out_vertices, uint32_t* out_count);

// Train pack inspection
bool edpc_inspect_pack_json(const char* pack_path, char** out_json);

// Vehicle mesh loading (.bbmodel & .obj) and resource reading
typedef struct EdpcVehicleMesh EdpcVehicleMesh;
EdpcVehicleMesh* edpc_vehicle_mesh_load(const char* pack_path, const char* model_resource);
EdpcVehicleMesh* edpc_vehicle_mesh_load_modular(
    const char* pack_path,
    const char* model_resource,
    const char* props_resource,
    const char* defs_resource,
    const char* bogie1_model,
    const char* bogie1_props,
    const char* bogie1_defs,
    float bogie1_pos,
    const char* bogie2_model,
    const char* bogie2_props,
    const char* bogie2_defs,
    float bogie2_pos
);
void edpc_vehicle_mesh_free(EdpcVehicleMesh* handle);
const EdpcVertex* edpc_vehicle_mesh_get_vertices(const EdpcVehicleMesh* handle, uint32_t* out_count);

bool edpc_read_pack_file(const char* pack_path, const char* resource_str, uint8_t** out_data, uint32_t* out_len);
void edpc_free_bytes(uint8_t* ptr, uint32_t len);

// Script generator & packager
bool edpc_generate_random_script_id(char** out_str);
bool edpc_generate_random_display_name(char** out_str);
bool edpc_preview_script_json(const char* request_json, char** out_script);
bool edpc_package_pack_json(const char* request_json, char** out_result_or_error);

// Memory cleanup
void edpc_free_string(char* ptr);

#ifdef __cplusplus
}
#endif

#endif // EDPC_CORE_H
