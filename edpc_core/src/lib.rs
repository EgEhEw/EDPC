pub mod kinematics;
pub mod obj_parser;
pub mod pack_inspector;
pub mod packager;
pub mod script_generator;
pub mod vehicle_parser;

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;

use kinematics::KinematicsSolver;
use obj_parser::{PantographModel, Vec3, Vertex};
use packager::PackageOptions;
use script_generator::ScriptConfig;
use serde::Deserialize;

pub struct EdpcModelHandle {
    pub model: PantographModel,
    pub solver: KinematicsSolver,
    pub cached_transformed_vertices: Vec<Vertex>,
}

pub struct EdpcVehicleMesh {
    pub vertices: Vec<Vertex>,
}

#[repr(C)]
pub struct EdpcPivots {
    pub base: Vec3,
    pub elbow: Vec3,
    pub tip: Vec3,
    pub shoe_offset: f32,
    pub has_p4: bool,
    pub p4_base: Vec3,
    pub p4_elbow: Vec3,
}

#[repr(C)]
pub struct EdpcAngles {
    pub angle1: f32,
    pub angle2: f32,
    pub angle4: f32,
    pub reachable: bool,
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_load_file(path_ptr: *const c_char) -> *mut EdpcModelHandle {
    if path_ptr.is_null() {
        return ptr::null_mut();
    }

    let c_str = CStr::from_ptr(path_ptr);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let content = match std::fs::read_to_string(path_str) {
        Ok(c) => c,
        Err(_) => return ptr::null_mut(),
    };

    edpc_model_load_string_internal(&content)
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_load_string(content_ptr: *const c_char) -> *mut EdpcModelHandle {
    if content_ptr.is_null() {
        return ptr::null_mut();
    }

    let c_str = CStr::from_ptr(content_ptr);
    let content = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    edpc_model_load_string_internal(content)
}

fn edpc_model_load_string_internal(content: &str) -> *mut EdpcModelHandle {
    let model = match PantographModel::from_obj_string(content) {
        Ok(m) => m,
        Err(_) => return ptr::null_mut(),
    };

    let solver = KinematicsSolver::new(&model.pivots);
    let initial_angles = solver.solve(model.pivots.tip.y);
    let initial_vertices = solver.transform_model(&model, &initial_angles);

    let handle = Box::new(EdpcModelHandle {
        model,
        solver,
        cached_transformed_vertices: initial_vertices,
    });

    Box::into_raw(handle)
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_free(handle: *mut EdpcModelHandle) {
    if !handle.is_null() {
        drop(Box::from_raw(handle));
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_get_pivots(
    handle: *const EdpcModelHandle,
    out_pivots: *mut EdpcPivots,
) -> bool {
    if handle.is_null() || out_pivots.is_null() {
        return false;
    }

    let h = &*handle;
    let p = &h.model.pivots;

    (*out_pivots).base = p.base;
    (*out_pivots).elbow = p.elbow;
    (*out_pivots).tip = p.tip;
    (*out_pivots).shoe_offset = p.shoe_offset;
    (*out_pivots).has_p4 = p.p4_base.is_some() && p.p4_elbow.is_some();
    (*out_pivots).p4_base = p.p4_base.unwrap_or_default();
    (*out_pivots).p4_elbow = p.p4_elbow.unwrap_or_default();

    true
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_solve_kinematics(
    handle: *const EdpcModelHandle,
    current_height: f32,
    out_angles: *mut EdpcAngles,
) -> bool {
    if handle.is_null() || out_angles.is_null() {
        return false;
    }

    let h = &*handle;
    let angles = h.solver.solve(current_height);

    (*out_angles).angle1 = angles.angle1;
    (*out_angles).angle2 = angles.angle2;
    (*out_angles).angle4 = angles.angle4;
    (*out_angles).reachable = angles.reachable;

    true
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_update_transformed_mesh(
    handle: *mut EdpcModelHandle,
    current_height: f32,
    out_vertices: *mut *const Vertex,
    out_count: *mut u32,
) -> bool {
    if handle.is_null() || out_vertices.is_null() || out_count.is_null() {
        return false;
    }

    let h = &mut *handle;
    let angles = h.solver.solve(current_height);
    h.cached_transformed_vertices = h.solver.transform_model(&h.model, &angles);

    *out_vertices = h.cached_transformed_vertices.as_ptr();
    *out_count = h.cached_transformed_vertices.len() as u32;

    true
}

#[no_mangle]
pub unsafe extern "C" fn edpc_inspect_pack_json(
    pack_path_ptr: *const c_char,
    out_json: *mut *mut c_char,
) -> bool {
    if pack_path_ptr.is_null() || out_json.is_null() {
        return false;
    }

    let c_str = CStr::from_ptr(pack_path_ptr);
    let pack_path = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    match pack_inspector::inspect_pack(pack_path) {
        Ok(res) => {
            if let Ok(json_str) = serde_json::to_string(&res) {
                if let Ok(c_string) = CString::new(json_str) {
                    *out_json = c_string.into_raw();
                    return true;
                }
            }
            false
        }
        Err(err) => {
            let err_json = serde_json::json!({ "error": err }).to_string();
            if let Ok(c_string) = CString::new(err_json) {
                *out_json = c_string.into_raw();
            }
            false
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_generate_random_script_id(out_str: *mut *mut c_char) -> bool {
    if out_str.is_null() {
        return false;
    }

    let id = ScriptConfig::new_random_id();
    if let Ok(c_string) = CString::new(id) {
        *out_str = c_string.into_raw();
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_generate_random_display_name(out_str: *mut *mut c_char) -> bool {
    if out_str.is_null() {
        return false;
    }

    let name = ScriptConfig::new_random_display_name();
    if let Ok(c_string) = CString::new(name) {
        *out_str = c_string.into_raw();
        true
    } else {
        false
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct PantographRequestItem {
    pub id: String,
    pub random_id: String,
    pub model_file_name: String,
    pub model_display_name: String,
    pub panto_obj_path: String,
    #[serde(default)]
    pub panto_mtl_path: Option<String>,
    #[serde(default)]
    pub panto_texture_path: Option<String>,
    pub wire_tracking_reference: f32,
    pub no_wire_park_height: f32,
    pub enable_depot_park: bool,
    pub depot_park_height: f32,
    pub upper_threshold: f32,
    pub lower_threshold: f32,
}

#[derive(Debug, Deserialize)]
struct PackageRequest {
    pack_input_path: String,
    pack_output_path: String,
    #[serde(default)]
    panto_obj_path: String,
    #[serde(default)]
    panto_mtl_path: Option<String>,
    #[serde(default)]
    panto_texture_path: Option<String>,
    custom_resources_rel_path: String,
    #[serde(default)]
    random_id: String,
    #[serde(default)]
    model_file_name: String,
    #[serde(default)]
    model_display_name: String,
    #[serde(default)]
    wire_tracking_reference: f32,
    #[serde(default)]
    no_wire_park_height: f32,
    #[serde(default)]
    enable_depot_park: bool,
    #[serde(default)]
    depot_park_height: f32,
    #[serde(default)]
    upper_threshold: f32,
    #[serde(default)]
    lower_threshold: f32,
    vehicles: Vec<pack_inspector::VehicleInfo>,
    #[serde(default)]
    pantographs: Vec<PantographRequestItem>,
}

#[no_mangle]
pub unsafe extern "C" fn edpc_package_pack_json(
    request_json_ptr: *const c_char,
    out_result_or_error: *mut *mut c_char,
) -> bool {
    if request_json_ptr.is_null() || out_result_or_error.is_null() {
        return false;
    }

    let c_str = CStr::from_ptr(request_json_ptr);
    let json_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    let req: PackageRequest = match serde_json::from_str(json_str) {
        Ok(r) => r,
        Err(e) => {
            let err = format!("Invalid request payload: {}", e);
            if let Ok(cs) = CString::new(err) {
                *out_result_or_error = cs.into_raw();
            }
            return false;
        }
    };

    let script_cfg = ScriptConfig {
        random_id: req.random_id.clone(),
        model_file_name: req.model_file_name.clone(),
        model_display_name: req.model_display_name.clone(),
        wire_tracking_reference: req.wire_tracking_reference,
        no_wire_park_height: req.no_wire_park_height,
        enable_depot_park: req.enable_depot_park,
        depot_park_height: req.depot_park_height,
        upper_threshold: req.upper_threshold,
        lower_threshold: req.lower_threshold,
        vehicles: req.vehicles.clone(),
    };

    let panto_package_items: Vec<packager::PantographPackageItem> = req.pantographs
        .into_iter()
        .map(|p| {
            let cfg = ScriptConfig {
                random_id: p.random_id.clone(),
                model_file_name: p.model_file_name.clone(),
                model_display_name: p.model_display_name.clone(),
                wire_tracking_reference: p.wire_tracking_reference,
                no_wire_park_height: p.no_wire_park_height,
                enable_depot_park: p.enable_depot_park,
                depot_park_height: p.depot_park_height,
                upper_threshold: p.upper_threshold,
                lower_threshold: p.lower_threshold,
                vehicles: req.vehicles.clone(),
            };
            packager::PantographPackageItem {
                id: p.id,
                random_id: p.random_id,
                model_file_name: p.model_file_name,
                model_display_name: p.model_display_name,
                panto_obj_path: p.panto_obj_path,
                panto_mtl_path: p.panto_mtl_path,
                panto_texture_path: p.panto_texture_path,
                script_config: cfg,
            }
        })
        .collect();

    let opts = PackageOptions {
        pack_input_path: req.pack_input_path,
        pack_output_path: req.pack_output_path,
        panto_obj_path: req.panto_obj_path,
        panto_mtl_path: req.panto_mtl_path,
        panto_texture_path: req.panto_texture_path,
        custom_resources_rel_path: req.custom_resources_rel_path,
        script_config: script_cfg,
        pantographs: panto_package_items,
    };

    match packager::package_train_pack(&opts) {
        Ok(res_path) => {
            if let Ok(cs) = CString::new(res_path) {
                *out_result_or_error = cs.into_raw();
                true
            } else {
                false
            }
        }
        Err(err) => {
            if let Ok(cs) = CString::new(err) {
                *out_result_or_error = cs.into_raw();
            }
            false
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_preview_script_json(
    request_json_ptr: *const c_char,
    out_script: *mut *mut c_char,
) -> bool {
    if request_json_ptr.is_null() || out_script.is_null() {
        return false;
    }

    let c_str = CStr::from_ptr(request_json_ptr);
    let json_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    let req: PackageRequest = match serde_json::from_str(json_str) {
        Ok(r) => r,
        Err(e) => {
            let err = format!("Invalid request payload: {}", e);
            if let Ok(cs) = CString::new(err) {
                *out_script = cs.into_raw();
            }
            return false;
        }
    };

    let mut combined = String::new();

    if !req.pantographs.is_empty() {
        for (idx, p) in req.pantographs.iter().enumerate() {
            let assigned: Vec<pack_inspector::VehicleInfo> = if req.pantographs.len() == 1 {
                req.vehicles.iter().filter(|v| v.has_pantograph).cloned().collect()
            } else {
                req.vehicles
                    .iter()
                    .filter(|v| {
                        v.has_pantograph
                            && (v.pantograph_id.as_deref() == Some(&p.id)
                                || v.pantograph_id.as_deref() == Some(&p.random_id))
                    })
                    .cloned()
                    .collect()
            };

            let cfg = ScriptConfig {
                random_id: p.random_id.clone(),
                model_file_name: p.model_file_name.clone(),
                model_display_name: p.model_display_name.clone(),
                wire_tracking_reference: p.wire_tracking_reference,
                no_wire_park_height: p.no_wire_park_height,
                enable_depot_park: p.enable_depot_park,
                depot_park_height: p.depot_park_height,
                upper_threshold: p.upper_threshold,
                lower_threshold: p.lower_threshold,
                vehicles: assigned,
            };

            if req.pantographs.len() > 1 {
                combined.push_str(&format!(
                    "// ============================================================================\n// PANTOGRAPH #{}: {} ({})\n// ============================================================================\n\n",
                    idx + 1,
                    p.model_display_name,
                    cfg.script_filename()
                ));
            }
            combined.push_str(&cfg.generate_javascript());
            combined.push_str("\n\n");
        }
    } else {
        let script_cfg = ScriptConfig {
            random_id: req.random_id,
            model_file_name: req.model_file_name,
            model_display_name: req.model_display_name,
            wire_tracking_reference: req.wire_tracking_reference,
            no_wire_park_height: req.no_wire_park_height,
            enable_depot_park: req.enable_depot_park,
            depot_park_height: req.depot_park_height,
            upper_threshold: req.upper_threshold,
            lower_threshold: req.lower_threshold,
            vehicles: req.vehicles,
        };
        combined = script_cfg.generate_javascript();
    }

    if let Ok(cs) = CString::new(combined) {
        *out_script = cs.into_raw();
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        drop(CString::from_raw(ptr));
    }
}

unsafe fn c_str_opt(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        None
    } else if let Ok(s) = CStr::from_ptr(ptr).to_str() {
        let trimmed = s.trim();
        if trimmed.is_empty() {
            None
        } else {
            Some(trimmed.to_string())
        }
    } else {
        None
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_vehicle_mesh_load(
    pack_path_ptr: *const c_char,
    model_resource_ptr: *const c_char,
) -> *mut EdpcVehicleMesh {
    edpc_vehicle_mesh_load_modular(
        pack_path_ptr,
        model_resource_ptr,
        ptr::null(),
        ptr::null(),
        ptr::null(),
        ptr::null(),
        ptr::null(),
        0.0,
        ptr::null(),
        ptr::null(),
        ptr::null(),
        0.0,
    )
}

#[no_mangle]
pub unsafe extern "C" fn edpc_vehicle_mesh_load_modular(
    pack_path_ptr: *const c_char,
    model_resource_ptr: *const c_char,
    props_resource_ptr: *const c_char,
    defs_resource_ptr: *const c_char,
    bogie1_model_ptr: *const c_char,
    bogie1_props_ptr: *const c_char,
    bogie1_defs_ptr: *const c_char,
    bogie1_pos: f32,
    bogie2_model_ptr: *const c_char,
    bogie2_props_ptr: *const c_char,
    bogie2_defs_ptr: *const c_char,
    bogie2_pos: f32,
) -> *mut EdpcVehicleMesh {
    if pack_path_ptr.is_null() || model_resource_ptr.is_null() {
        return ptr::null_mut();
    }
    let pack_path = match CStr::from_ptr(pack_path_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };
    let model_res = match CStr::from_ptr(model_resource_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    let props_res = c_str_opt(props_resource_ptr);
    let defs_res = c_str_opt(defs_resource_ptr);
    let bogie1_model = c_str_opt(bogie1_model_ptr);
    let bogie1_props = c_str_opt(bogie1_props_ptr);
    let bogie1_defs = c_str_opt(bogie1_defs_ptr);
    let bogie2_model = c_str_opt(bogie2_model_ptr);
    let bogie2_props = c_str_opt(bogie2_props_ptr);
    let bogie2_defs = c_str_opt(bogie2_defs_ptr);

    match vehicle_parser::load_vehicle_mesh_modular(
        pack_path,
        model_res,
        props_res.as_deref(),
        defs_res.as_deref(),
        bogie1_model.as_deref(),
        bogie1_props.as_deref(),
        bogie1_defs.as_deref(),
        bogie1_pos,
        bogie2_model.as_deref(),
        bogie2_props.as_deref(),
        bogie2_defs.as_deref(),
        bogie2_pos,
    ) {
        Ok(vertices) => {
            let mesh = Box::new(EdpcVehicleMesh { vertices });
            Box::into_raw(mesh)
        }
        Err(_) => ptr::null_mut(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_vehicle_mesh_free(handle: *mut EdpcVehicleMesh) {
    if !handle.is_null() {
        drop(Box::from_raw(handle));
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_vehicle_mesh_get_vertices(
    handle: *const EdpcVehicleMesh,
    out_count: *mut u32,
) -> *const Vertex {
    if handle.is_null() || out_count.is_null() {
        return ptr::null();
    }
    let h = &*handle;
    *out_count = h.vertices.len() as u32;
    h.vertices.as_ptr()
}

#[no_mangle]
pub unsafe extern "C" fn edpc_read_pack_file(
    pack_path_ptr: *const c_char,
    resource_ptr: *const c_char,
    out_data: *mut *mut u8,
    out_len: *mut u32,
) -> bool {
    if pack_path_ptr.is_null() || resource_ptr.is_null() || out_data.is_null() || out_len.is_null() {
        return false;
    }
    let pack_path = match CStr::from_ptr(pack_path_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let resource_str = match CStr::from_ptr(resource_ptr).to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    match vehicle_parser::read_pack_file(pack_path, resource_str) {
        Ok(bytes) => {
            *out_len = bytes.len() as u32;
            let mut boxed = bytes.into_boxed_slice();
            *out_data = boxed.as_mut_ptr();
            std::mem::forget(boxed);
            true
        }
        Err(_) => false,
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_free_bytes(ptr: *mut u8, len: u32) {
    if !ptr.is_null() && len > 0 {
        let slice = std::slice::from_raw_parts_mut(ptr, len as usize);
        drop(Box::from_raw(slice as *mut [u8]));
    }
}

