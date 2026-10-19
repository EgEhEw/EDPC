pub mod bbmodel_converter;
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
#[derive(Debug, Clone, Copy)]
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
pub struct EdpcTransformedPivots {
    pub p1: Vec3,
    pub p2: Vec3,
    pub p3: Vec3,
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

pub fn update_obj_pivots(content: &str, p: &EdpcPivots) -> Option<String> {
    // 1. Parse original pivots from OBJ to calculate deltas
    let orig_model = PantographModel::from_obj_string(content).ok();
    let orig_base = orig_model.as_ref().map(|m| m.pivots.base).unwrap_or(p.base);
    let orig_p4_base = orig_model.as_ref().and_then(|m| m.pivots.p4_base);
    let orig_p4_elbow = orig_model.as_ref().and_then(|m| m.pivots.p4_elbow);

    let delta_p1 = Vec3::new(p.base.x - orig_base.x, p.base.y - orig_base.y, p.base.z - orig_base.z);
    let delta_p4 = if let (true, Some(orig_b)) = (p.has_p4, orig_p4_base) {
        Vec3::new(p.p4_base.x - orig_b.x, p.p4_base.y - orig_b.y, p.p4_base.z - orig_b.z)
    } else {
        Vec3::default()
    };

    let shift_p1 = delta_p1.x.abs() > 1e-6 || delta_p1.y.abs() > 1e-6 || delta_p1.z.abs() > 1e-6;

    // Check if p4 can be mapped between base and elbow hinges
    let can_map_p4_obj = if p.has_p4 {
        if let (Some(ob), Some(oe)) = (orig_p4_base, orig_p4_elbow) {
            let dy_orig = oe.y - ob.y;
            let dz_orig = oe.z - ob.z;
            let lo = dy_orig.hypot(dz_orig);

            let dy_new = p.p4_elbow.y - p.p4_base.y;
            let dz_new = p.p4_elbow.z - p.p4_base.z;
            let ln = dy_new.hypot(dz_new);

            if lo > 1e-4 && ln > 1e-4 {
                let to = dy_orig.atan2(dz_orig);
                let tn = dy_new.atan2(dz_new);
                let changed = (ob.x - p.p4_base.x).abs() > 1e-6
                    || (ob.y - p.p4_base.y).abs() > 1e-6
                    || (ob.z - p.p4_base.z).abs() > 1e-6
                    || (oe.y - p.p4_elbow.y).abs() > 1e-6
                    || (oe.z - p.p4_elbow.z).abs() > 1e-6;
                if changed {
                    Some((ob, to, tn, ln / lo))
                } else {
                    None
                }
            } else {
                None
            }
        } else {
            None
        }
    } else {
        None
    };

    let shift_p4_rigid = can_map_p4_obj.is_none() && (delta_p4.x.abs() > 1e-6 || delta_p4.y.abs() > 1e-6 || delta_p4.z.abs() > 1e-6);

    // 2. Identify which vertex indices belong to p1 and p4
    let mut p1_v_indices = std::collections::HashSet::new();
    let mut p4_v_indices = std::collections::HashSet::new();
    let mut cur_group = "p_base".to_string();
    let mut v_idx = 0usize;

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("g ") {
            let g = trimmed[2..].trim();
            if !g.is_empty() {
                cur_group = PantographModel::normalize_group_name(g);
            }
        } else if trimmed.starts_with("o ") {
            let o = trimmed[2..].trim();
            let normalized = PantographModel::normalize_group_name(o);
            let is_panto_part = normalized == "p_base" || normalized == "p1" || normalized == "p2" || normalized == "p3" || normalized == "p4";
            let cur_is_panto_part = cur_group == "p_base" || cur_group == "p1" || cur_group == "p2" || cur_group == "p3" || cur_group == "p4";
            if is_panto_part || !cur_is_panto_part {
                cur_group = normalized;
            }
        } else if trimmed.starts_with("v ") {
            v_idx += 1;
            if cur_group == "p1" {
                p1_v_indices.insert(v_idx);
            } else if cur_group == "p4" {
                p4_v_indices.insert(v_idx);
            }
        } else if trimmed.starts_with("f ") {
            for token in trimmed[2..].split_whitespace() {
                let sub = token.split('/').next().unwrap_or("");
                if let Ok(idx) = sub.parse::<isize>() {
                    let actual_idx = if idx > 0 {
                        idx as usize
                    } else if idx < 0 {
                        (v_idx as isize + idx + 1) as usize
                    } else {
                        0
                    };
                    if actual_idx > 0 {
                        if cur_group == "p1" {
                            p1_v_indices.insert(actual_idx);
                        } else if cur_group == "p4" {
                            p4_v_indices.insert(actual_idx);
                        }
                    }
                }
            }
        }
    }

    // 3. Rebuild lines with shifted vertices and remove existing pivot headers
    let mut clean_lines: Vec<String> = Vec::new();
    let mut v_counter = 0usize;

    for line in content.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("# pivot ") || trimmed.starts_with("# PANTOGRAPH PIVOT DATA") {
            continue;
        }

        if trimmed.starts_with("v ") {
            v_counter += 1;
            if let Some((ob, theta_orig, theta_new, scale)) = can_map_p4_obj {
                if p4_v_indices.contains(&v_counter) {
                    let parts: Vec<&str> = trimmed[2..].split_whitespace().collect();
                    if parts.len() >= 3 {
                        if let (Ok(x), Ok(y), Ok(z)) = (
                            parts[0].parse::<f32>(),
                            parts[1].parse::<f32>(),
                            parts[2].parse::<f32>(),
                        ) {
                            let y_rel = y - ob.y;
                            let z_rel = z - ob.z;

                            let t_long = z_rel * theta_orig.cos() + y_rel * theta_orig.sin();
                            let t_perp = -z_rel * theta_orig.sin() + y_rel * theta_orig.cos();

                            let t_long_scaled = t_long * scale;

                            let ny = p.p4_base.y + t_long_scaled * theta_new.sin() + t_perp * theta_new.cos();
                            let nz = p.p4_base.z + t_long_scaled * theta_new.cos() - t_perp * theta_new.sin();
                            let nx = p.p4_base.x + (x - ob.x);

                            if parts.len() > 3 {
                                clean_lines.push(format!("v {:.6} {:.6} {:.6} {}", nx, ny, nz, parts[3..].join(" ")));
                            } else {
                                clean_lines.push(format!("v {:.6} {:.6} {:.6}", nx, ny, nz));
                            }
                            continue;
                        }
                    }
                }
            } else if shift_p4_rigid && p4_v_indices.contains(&v_counter) {
                let parts: Vec<&str> = trimmed[2..].split_whitespace().collect();
                if parts.len() >= 3 {
                    if let (Ok(x), Ok(y), Ok(z)) = (
                        parts[0].parse::<f32>(),
                        parts[1].parse::<f32>(),
                        parts[2].parse::<f32>(),
                    ) {
                        let nx = x + delta_p4.x;
                        let ny = y + delta_p4.y;
                        let nz = z + delta_p4.z;
                        if parts.len() > 3 {
                            clean_lines.push(format!("v {:.6} {:.6} {:.6} {}", nx, ny, nz, parts[3..].join(" ")));
                        } else {
                            clean_lines.push(format!("v {:.6} {:.6} {:.6}", nx, ny, nz));
                        }
                        continue;
                    }
                }
            } else if shift_p1 && p1_v_indices.contains(&v_counter) {
                let parts: Vec<&str> = trimmed[2..].split_whitespace().collect();
                if parts.len() >= 3 {
                    if let (Ok(x), Ok(y), Ok(z)) = (
                        parts[0].parse::<f32>(),
                        parts[1].parse::<f32>(),
                        parts[2].parse::<f32>(),
                    ) {
                        let nx = x + delta_p1.x;
                        let ny = y + delta_p1.y;
                        let nz = z + delta_p1.z;
                        if parts.len() > 3 {
                            clean_lines.push(format!("v {:.6} {:.6} {:.6} {}", nx, ny, nz, parts[3..].join(" ")));
                        } else {
                            clean_lines.push(format!("v {:.6} {:.6} {:.6}", nx, ny, nz));
                        }
                        continue;
                    }
                }
            }
        }

        clean_lines.push(line.to_string());
    }

    let mut pivot_headers = Vec::new();
    pivot_headers.push(format!("# pivot p1 {:.5} {:.5} {:.5}", p.base.x, p.base.y, p.base.z));
    pivot_headers.push(format!("# pivot p2 {:.5} {:.5} {:.5}", p.elbow.x, p.elbow.y, p.elbow.z));
    pivot_headers.push(format!("# pivot p3 {:.5} {:.5} {:.5}", p.tip.x, p.tip.y, p.tip.z));
    if p.has_p4 {
        pivot_headers.push(format!("# pivot p4 {:.5} {:.5} {:.5}", p.p4_base.x, p.p4_base.y, p.p4_base.z));
        pivot_headers.push(format!("# pivot p4_elbow {:.5} {:.5} {:.5}", p.p4_elbow.x, p.p4_elbow.y, p.p4_elbow.z));
    }

    let mut target_idx = 0;
    for (i, line) in clean_lines.iter().enumerate() {
        let trimmed = line.trim();
        if trimmed.starts_with("mtllib ") || trimmed.starts_with("# Made in Blockbench") {
            target_idx = i + 1;
        }
    }

    clean_lines.insert(target_idx, "".to_string());
    clean_lines.insert(target_idx + 1, "# PANTOGRAPH PIVOT DATA (auto-generated by EDPC)".to_string());
    let mut offset = 2;
    for h in pivot_headers {
        clean_lines.insert(target_idx + offset, h);
        offset += 1;
    }
    clean_lines.insert(target_idx + offset, "".to_string());

    let mut result = clean_lines.join("\n");
    if !result.ends_with('\n') {
        result.push('\n');
    }
    Some(result)
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_set_pivots(
    handle: *mut EdpcModelHandle,
    pivots: *const EdpcPivots,
) -> bool {
    if handle.is_null() || pivots.is_null() {
        return false;
    }

    let h = &mut *handle;
    let p = &*pivots;

    h.model.pivots.base = p.base;
    h.model.pivots.elbow = p.elbow;
    h.model.pivots.tip = p.tip;
    h.model.pivots.shoe_offset = p.shoe_offset;
    if p.has_p4 {
        h.model.pivots.p4_base = Some(p.p4_base);
        h.model.pivots.p4_elbow = Some(p.p4_elbow);
    } else {
        h.model.pivots.p4_base = None;
        h.model.pivots.p4_elbow = None;
    }

    h.solver = KinematicsSolver::new(&h.model.pivots);
    h.cached_transformed_vertices.clear();

    true
}

#[no_mangle]
pub unsafe extern "C" fn edpc_save_pivots_to_obj(
    obj_path_ptr: *const c_char,
    pivots: *const EdpcPivots,
) -> bool {
    if obj_path_ptr.is_null() || pivots.is_null() {
        return false;
    }

    let c_str = CStr::from_ptr(obj_path_ptr);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };

    let content = match std::fs::read_to_string(path_str) {
        Ok(c) => c,
        Err(_) => return false,
    };

    let p = &*pivots;
    let new_content = match update_obj_pivots(&content, p) {
        Some(s) => s,
        None => return false,
    };

    std::fs::write(path_str, new_content).is_ok()
}

#[no_mangle]
pub unsafe extern "C" fn edpc_convert_or_fix_bbmodel(bbmodel_path_ptr: *const c_char) -> *mut c_char {
    if bbmodel_path_ptr.is_null() {
        return ptr::null_mut();
    }
    let c_str = CStr::from_ptr(bbmodel_path_ptr);
    let path_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return ptr::null_mut(),
    };

    match bbmodel_converter::convert_or_fix_bbmodel(path_str) {
        Ok(out_path) => {
            match CString::new(out_path) {
                Ok(cs) => cs.into_raw(),
                Err(_) => ptr::null_mut(),
            }
        }
        Err(e) => {
            eprintln!("edpc_convert_or_fix_bbmodel error: {}", e);
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn edpc_model_get_transformed_pivots(
    handle: *const EdpcModelHandle,
    current_height: f32,
    out_pivots: *mut EdpcTransformedPivots,
) -> bool {
    if handle.is_null() || out_pivots.is_null() {
        return false;
    }
    let h = &*handle;
    let angles = h.solver.solve(current_height);
    let base = h.model.pivots.base;
    let elbow = h.model.pivots.elbow;
    let tip = h.model.pivots.tip;

    let a1 = angles.angle1;
    let a2 = angles.angle2;
    let _a4 = angles.angle4;

    // p1 is base
    let p1 = base;

    // p2 is elbow rotated around base by a1
    let dy2 = elbow.y - base.y;
    let dz2 = elbow.z - base.z;
    let p2 = Vec3 {
        x: 0.0,
        y: base.y + dy2 * a1.cos() - dz2 * a1.sin(),
        z: base.z + dy2 * a1.sin() + dz2 * a1.cos(),
    };

    // p3 is tip: y is current_height, z is adaptive target_z
    let target_z = if current_height < 1.10 {
        let t_fold = ((1.10 - current_height) / (1.10 - 0.40)).clamp(0.0, 1.0);
        let smooth_fold = t_fold * t_fold * (3.0 - 2.0 * t_fold);
        let shift_z = if elbow.z < base.z { -0.035 } else { 0.035 };
        tip.z + smooth_fold * shift_z
    } else {
        tip.z
    };
    let p3 = Vec3 {
        x: 0.0,
        y: current_height,
        z: target_z,
    };

    let (has_p4, p4_b, p4_e) = if let (Some(b4), Some(e4)) = (h.model.pivots.p4_base, h.model.pivots.p4_elbow) {
        let dy_cb = e4.y - elbow.y;
        let dz_cb = e4.z - elbow.z;
        let cy2 = elbow.y + dy_cb * a2.cos() - dz_cb * a2.sin();
        let cz2 = elbow.z + dy_cb * a2.sin() + dz_cb * a2.cos();

        let dy_ba = cy2 - base.y;
        let dz_ba = cz2 - base.z;
        let cy_world = base.y + dy_ba * a1.cos() - dz_ba * a1.sin();
        let cz_world = base.z + dy_ba * a1.sin() + dz_ba * a1.cos();
        let p4_elbow_dyn = Vec3 { x: 0.0, y: cy_world, z: cz_world };
        (true, Vec3 { x: 0.0, y: b4.y, z: b4.z }, p4_elbow_dyn)
    } else {
        (false, Vec3::default(), Vec3::default())
    };

    (*out_pivots).p1 = Vec3 { x: 0.0, y: p1.y, z: p1.z };
    (*out_pivots).p2 = p2;
    (*out_pivots).p3 = p3;
    (*out_pivots).has_p4 = has_p4;
    (*out_pivots).p4_base = p4_b;
    (*out_pivots).p4_elbow = p4_e;

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
pub struct CustomPivotsJson {
    pub base_x: f32,
    pub base_y: f32,
    pub base_z: f32,
    pub elbow_x: f32,
    pub elbow_y: f32,
    pub elbow_z: f32,
    pub tip_x: f32,
    pub tip_y: f32,
    pub tip_z: f32,
    #[serde(default)]
    pub shoe_offset: f32,
    #[serde(default)]
    pub has_p4: bool,
    #[serde(default)]
    pub p4_base_x: f32,
    #[serde(default)]
    pub p4_base_y: f32,
    #[serde(default)]
    pub p4_base_z: f32,
    #[serde(default)]
    pub p4_elbow_x: f32,
    #[serde(default)]
    pub p4_elbow_y: f32,
    #[serde(default)]
    pub p4_elbow_z: f32,
}

impl CustomPivotsJson {
    pub fn to_edpc_pivots(&self) -> EdpcPivots {
        EdpcPivots {
            base: Vec3::new(self.base_x, self.base_y, self.base_z),
            elbow: Vec3::new(self.elbow_x, self.elbow_y, self.elbow_z),
            tip: Vec3::new(self.tip_x, self.tip_y, self.tip_z),
            shoe_offset: self.shoe_offset,
            has_p4: self.has_p4,
            p4_base: Vec3::new(self.p4_base_x, self.p4_base_y, self.p4_base_z),
            p4_elbow: Vec3::new(self.p4_elbow_x, self.p4_elbow_y, self.p4_elbow_z),
        }
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
    #[serde(default)]
    pub custom_pivots: Option<CustomPivotsJson>,
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
    #[serde(default)]
    custom_pivots: Option<CustomPivotsJson>,
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
                custom_pivots: p.custom_pivots.as_ref().map(|cp| cp.to_edpc_pivots()),
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
        custom_pivots: req.custom_pivots.as_ref().map(|cp| cp.to_edpc_pivots()),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_update_obj_pivots_and_set_pivots() {
        let sample_obj = r#"# Made in Blockbench
mtllib sample.mtl

# PANTOGRAPH PIVOT DATA (auto-generated by EDPC)
# pivot p1 0.00000 0.18357 0.11706
# pivot p2 0.00000 0.83591 -1.21875
# pivot p3 0.00000 1.42328 0.08750

g p_base
v 0.0 0.0 0.0
vn 0.0 1.0 0.0
vt 0.0 0.0
f 1/1/1 1/1/1 1/1/1
"#;

        let new_pivots = EdpcPivots {
            base: Vec3::new(0.0, 0.20000, 0.15000),
            elbow: Vec3::new(0.0, 0.85000, -1.20000),
            tip: Vec3::new(0.0, 1.45000, 0.09000),
            shoe_offset: 0.1048,
            has_p4: true,
            p4_base: Vec3::new(0.0, 0.22000, -0.30000),
            p4_elbow: Vec3::new(0.0, 0.76000, -1.35000),
        };

        let updated = update_obj_pivots(sample_obj, &new_pivots).expect("Failed to update OBJ pivots");
        assert!(updated.contains("# pivot p1 0.00000 0.20000 0.15000"));
        assert!(updated.contains("# pivot p2 0.00000 0.85000 -1.20000"));
        assert!(updated.contains("# pivot p3 0.00000 1.45000 0.09000"));
        assert!(updated.contains("# pivot p4 0.00000 0.22000 -0.30000"));
        assert!(updated.contains("# pivot p4_elbow 0.00000 0.76000 -1.35000"));
        // Old pivots should not exist
        assert!(!updated.contains("# pivot p1 0.00000 0.18357 0.11706"));

        // Now test loading model and setting pivots
        let c_obj = CString::new(updated).unwrap();
        let handle = unsafe { edpc_model_load_string(c_obj.as_ptr()) };
        assert!(!handle.is_null());

        let mut read_pivots = EdpcPivots {
            base: Vec3::default(),
            elbow: Vec3::default(),
            tip: Vec3::default(),
            shoe_offset: 0.0,
            has_p4: false,
            p4_base: Vec3::default(),
            p4_elbow: Vec3::default(),
        };
        unsafe {
            assert!(edpc_model_get_pivots(handle, &mut read_pivots));
            assert_eq!(read_pivots.base.y, 0.20000);
            assert_eq!(read_pivots.elbow.y, 0.85000);
            assert!(read_pivots.has_p4);

            // Change pivots dynamically via edpc_model_set_pivots
            let custom_pivots = EdpcPivots {
                base: Vec3::new(0.0, 0.25000, 0.16000),
                elbow: Vec3::new(0.0, 0.90000, -1.15000),
                tip: Vec3::new(0.0, 1.50000, 0.10000),
                shoe_offset: 0.1048,
                has_p4: false,
                p4_base: Vec3::default(),
                p4_elbow: Vec3::default(),
            };
            assert!(edpc_model_set_pivots(handle, &custom_pivots));

            let mut verify_pivots = EdpcPivots {
                base: Vec3::default(),
                elbow: Vec3::default(),
                tip: Vec3::default(),
                shoe_offset: 0.0,
                has_p4: false,
                p4_base: Vec3::default(),
                p4_elbow: Vec3::default(),
            };
            assert!(edpc_model_get_pivots(handle, &mut verify_pivots));
            assert_eq!(verify_pivots.base.y, 0.25000);
            assert_eq!(verify_pivots.elbow.y, 0.90000);
            assert!(!verify_pivots.has_p4);

            edpc_model_free(handle);
        }
    }

    #[test]
    fn test_update_obj_pivots_shifts_p4_vertices() {
        let sample_obj = r#"# Made in Blockbench
mtllib sample.mtl

# PANTOGRAPH PIVOT DATA (auto-generated by EDPC)
# pivot p1 0.00000 0.20000 0.15000
# pivot p2 0.00000 0.85000 -1.20000
# pivot p3 0.00000 1.45000 0.09000
# pivot p4 0.00000 0.44800 0.12340
# pivot p4_elbow 0.00000 0.76000 -1.35000

g p_base
v 0.000000 0.000000 0.000000
f 1/1/1 1/1/1 1/1/1

g p4
v 0.000000 0.448000 0.123400
v 0.000000 0.760000 -1.350000
f 2/1/1 3/1/1 2/1/1
"#;

        let new_pivots = EdpcPivots {
            base: Vec3::new(0.0, 0.20000, 0.15000),
            elbow: Vec3::new(0.0, 0.85000, -1.20000),
            tip: Vec3::new(0.0, 1.45000, 0.09000),
            shoe_offset: 0.1048,
            has_p4: true,
            p4_base: Vec3::new(0.0, 0.40000, 0.12340), // Lowered by -0.048 in Y
            p4_elbow: Vec3::new(0.0, 0.76000, -1.35000), // Knee kept intact
        };

        let updated = update_obj_pivots(sample_obj, &new_pivots).expect("Failed to update OBJ pivots");
        // Verify pivot header
        assert!(updated.contains("# pivot p4 0.00000 0.40000 0.12340"));
        assert!(updated.contains("# pivot p4_elbow 0.00000 0.76000 -1.35000"));

        // Verify that p_base vertex is NOT changed
        assert!(updated.contains("v 0.000000 0.000000 0.000000"));

        // Verify that p4 lower hinge vertex moved down to match p4_base:
        assert!(updated.contains("v 0.000000 0.400000 0.123400"));
        // Verify that p4 upper hinge vertex stayed attached to p4_elbow:
        assert!(updated.contains("v 0.000000 0.760000 -1.350000"));
    }
}


