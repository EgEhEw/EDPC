use std::collections::HashMap;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use serde::{Deserialize, Serialize};
use zip::ZipArchive;

use crate::obj_parser::{Vec2, Vec3, Vertex};

/// MTR Model Properties definition from properties.json
#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct ModelProperties {
    #[serde(default)]
    pub parts: Vec<PartProperty>,
    #[serde(rename = "modelYOffset", default)]
    pub model_y_offset: f32,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct PartProperty {
    #[serde(default)]
    pub names: Vec<String>,
    #[serde(rename = "positionDefinitions", default)]
    pub position_definitions: Vec<String>,
    #[serde(default = "default_condition")]
    pub condition: String,
    #[serde(rename = "renderStage", default)]
    pub render_stage: String,
    #[serde(rename = "type", default)]
    pub part_type: String,
}

fn default_condition() -> String {
    "NORMAL".to_string()
}

/// MTR Position Definitions file from definition.json
#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct PositionDefinitionFile {
    #[serde(rename = "positionDefinitions", default)]
    pub position_definitions: Vec<PositionDefinition>,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct PositionDefinition {
    pub name: String,
    #[serde(default)]
    pub positions: Vec<Vec3Def>,
    #[serde(rename = "positionsFlipped", default)]
    pub positions_flipped: Vec<Vec3Def>,
}

#[derive(Debug, Clone, Copy, Deserialize, Serialize)]
pub struct Vec3Def {
    #[serde(default)]
    pub x: f32,
    #[serde(default)]
    pub y: f32,
    #[serde(default)]
    pub z: f32,
}

/// Check if a condition is active for the forward train car 3D preview
pub fn is_condition_active(cond: &str) -> bool {
    let c = cond.trim().to_uppercase();
    match c.as_str() {
        "NORMAL" | "ON_ROUTE_FORWARDS" | "" => true,
        "ON_ROUTE_BACKWARDS" | "AT_DEPOT" => false,
        _ => true,
    }
}

/// Check if a part should be skipped for train car preview (e.g. inter-car articulation bellows/gangways)
pub fn is_part_skipped(part_names: &[String]) -> bool {
    part_names.iter().any(|pn| {
        let lower = pn.trim().to_lowercase();
        lower.contains("articular") || lower.contains("gangway")
    })
}

pub fn parse_properties_json(json_str: &str) -> Result<ModelProperties, String> {
    serde_json::from_str(json_str).map_err(|e| format!("Failed to parse model properties JSON: {}", e))
}

pub fn parse_definitions_json(json_str: &str) -> Result<PositionDefinitionFile, String> {
    serde_json::from_str(json_str).map_err(|e| format!("Failed to parse position definitions JSON: {}", e))
}

#[derive(Clone)]
struct CubeFace {
    corners_local: [Vec3; 4],
    normal_local: Vec3,
    uvs: [Vec2; 4],
}

struct ParsedCube {
    name: String,
    #[allow(dead_code)]
    uuid: String,
    ancestors: Vec<String>,
    faces: Vec<CubeFace>,
}

/// Parse Blockbench (.bbmodel) JSON string into a 3D vertex mesh with modular part placement
pub fn parse_bbmodel_modular(
    json_str: &str,
    props_opt: Option<&ModelProperties>,
    defs_opt: Option<&PositionDefinitionFile>,
) -> Result<Vec<Vertex>, String> {
    let root: serde_json::Value = serde_json::from_str(json_str)
        .map_err(|e| format!("Failed to parse bbmodel JSON: {}", e))?;

    let res_w = root
        .get("resolution")
        .and_then(|r| r.get("width"))
        .and_then(|w| w.as_f64())
        .unwrap_or(1024.0) as f32;
    let res_h = root
        .get("resolution")
        .and_then(|r| r.get("height"))
        .and_then(|h| h.as_f64())
        .unwrap_or(1024.0) as f32;

    let elements = root
        .get("elements")
        .and_then(|e| e.as_array())
        .ok_or_else(|| "Missing 'elements' array in bbmodel".to_string())?;

    // 1. Build group UUID to name mapping
    let mut groups_by_uuid: HashMap<String, String> = HashMap::new();
    if let Some(groups) = root.get("groups").and_then(|g| g.as_array()) {
        for g in groups {
            if let (Some(uuid), Some(name)) = (
                g.get("uuid").and_then(|u| u.as_str()),
                g.get("name").and_then(|n| n.as_str()),
            ) {
                groups_by_uuid.insert(uuid.to_string(), name.to_string());
            }
        }
    }

    // 2. Map each element to its hierarchy of ancestor group names
    let mut element_ancestors: HashMap<String, Vec<String>> = HashMap::new();
    fn walk_outliner(
        node: &serde_json::Value,
        ancestors: &[String],
        groups_by_uuid: &HashMap<String, String>,
        out_map: &mut HashMap<String, Vec<String>>,
    ) {
        if let Some(obj) = node.as_object() {
            let mut new_anc = ancestors.to_vec();
            if let Some(uuid) = obj.get("uuid").and_then(|u| u.as_str()) {
                if let Some(name) = groups_by_uuid.get(uuid) {
                    if !new_anc.iter().any(|a| a.eq_ignore_ascii_case(name)) {
                        new_anc.push(name.clone());
                    }
                }
            }
            if let Some(name) = obj.get("name").and_then(|n| n.as_str()) {
                if !new_anc.iter().any(|a| a.eq_ignore_ascii_case(name)) {
                    new_anc.push(name.to_string());
                }
            }
            if let Some(children) = obj.get("children").and_then(|c| c.as_array()) {
                for child in children {
                    walk_outliner(child, &new_anc, groups_by_uuid, out_map);
                }
            }
        } else if let Some(uuid_str) = node.as_str() {
            out_map.insert(uuid_str.to_string(), ancestors.to_vec());
        }
    }

    if let Some(outliner) = root.get("outliner").and_then(|o| o.as_array()) {
        for node in outliner {
            walk_outliner(node, &[], &groups_by_uuid, &mut element_ancestors);
        }
    }

    // 3. Parse all visible/exported cubes in local coordinate space
    let mut parsed_cubes: Vec<ParsedCube> = Vec::new();

    for el in elements {
        if el.get("export").and_then(|x| x.as_bool()) == Some(false) {
            continue;
        }

        let from_arr = match el.get("from").and_then(|f| f.as_array()) {
            Some(a) if a.len() >= 3 => a,
            _ => continue,
        };
        let to_arr = match el.get("to").and_then(|t| t.as_array()) {
            Some(a) if a.len() >= 3 => a,
            _ => continue,
        };

        let x1 = from_arr[0].as_f64().unwrap_or(0.0) as f32;
        let y1 = from_arr[1].as_f64().unwrap_or(0.0) as f32;
        let z1 = from_arr[2].as_f64().unwrap_or(0.0) as f32;

        let x2 = to_arr[0].as_f64().unwrap_or(0.0) as f32;
        let y2 = to_arr[1].as_f64().unwrap_or(0.0) as f32;
        let z2 = to_arr[2].as_f64().unwrap_or(0.0) as f32;

        let x_min = x1.min(x2);
        let x_max = x1.max(x2);
        let y_min = y1.min(y2);
        let y_max = y1.max(y2);
        let z_min = z1.min(z2);
        let z_max = z1.max(z2);

        let origin = if let Some(orig_arr) = el.get("origin").and_then(|o| o.as_array()) {
            if orig_arr.len() >= 3 {
                Vec3::new(
                    orig_arr[0].as_f64().unwrap_or(0.0) as f32,
                    orig_arr[1].as_f64().unwrap_or(0.0) as f32,
                    orig_arr[2].as_f64().unwrap_or(0.0) as f32,
                )
            } else {
                Vec3::default()
            }
        } else {
            Vec3::default()
        };

        let (rx, ry, rz) = if let Some(rot_arr) = el.get("rotation").and_then(|r| r.as_array()) {
            if rot_arr.len() >= 3 {
                (
                    rot_arr[0].as_f64().unwrap_or(0.0) as f32,
                    rot_arr[1].as_f64().unwrap_or(0.0) as f32,
                    rot_arr[2].as_f64().unwrap_or(0.0) as f32,
                )
            } else {
                (0.0, 0.0, 0.0)
            }
        } else {
            (0.0, 0.0, 0.0)
        };

        let has_rot = rx.abs() > 1e-4 || ry.abs() > 1e-4 || rz.abs() > 1e-4;

        let faces = match el.get("faces").and_then(|f| f.as_object()) {
            Some(f) => f,
            None => continue,
        };

        let face_defs: [(&str, Vec3, [Vec3; 4]); 6] = [
            (
                "east",
                Vec3::new(1.0, 0.0, 0.0),
                [
                    Vec3::new(x_max, y_max, z_max),
                    Vec3::new(x_max, y_max, z_min),
                    Vec3::new(x_max, y_min, z_max),
                    Vec3::new(x_max, y_min, z_min),
                ],
            ),
            (
                "west",
                Vec3::new(-1.0, 0.0, 0.0),
                [
                    Vec3::new(x_min, y_max, z_min),
                    Vec3::new(x_min, y_max, z_max),
                    Vec3::new(x_min, y_min, z_min),
                    Vec3::new(x_min, y_min, z_max),
                ],
            ),
            (
                "up",
                Vec3::new(0.0, 1.0, 0.0),
                [
                    Vec3::new(x_min, y_max, z_min),
                    Vec3::new(x_max, y_max, z_min),
                    Vec3::new(x_min, y_max, z_max),
                    Vec3::new(x_max, y_max, z_max),
                ],
            ),
            (
                "down",
                Vec3::new(0.0, -1.0, 0.0),
                [
                    Vec3::new(x_min, y_min, z_max),
                    Vec3::new(x_max, y_min, z_max),
                    Vec3::new(x_min, y_min, z_min),
                    Vec3::new(x_max, y_min, z_min),
                ],
            ),
            (
                "south",
                Vec3::new(0.0, 0.0, 1.0),
                [
                    Vec3::new(x_min, y_max, z_max),
                    Vec3::new(x_max, y_max, z_max),
                    Vec3::new(x_min, y_min, z_max),
                    Vec3::new(x_max, y_min, z_max),
                ],
            ),
            (
                "north",
                Vec3::new(0.0, 0.0, -1.0),
                [
                    Vec3::new(x_max, y_max, z_min),
                    Vec3::new(x_min, y_max, z_min),
                    Vec3::new(x_max, y_min, z_min),
                    Vec3::new(x_min, y_min, z_min),
                ],
            ),
        ];

        let mut cube_faces = Vec::new();

        for (fname, norm, corners) in &face_defs {
            if let Some(f_val) = faces.get(*fname) {
                let uv_arr = f_val.get("uv").and_then(|u| u.as_array());
                let (u1, v1, u2, v2) = if let Some(ua) = uv_arr {
                    if ua.len() >= 4 {
                        (
                            ua[0].as_f64().unwrap_or(0.0) as f32,
                            ua[1].as_f64().unwrap_or(0.0) as f32,
                            ua[2].as_f64().unwrap_or(0.0) as f32,
                            ua[3].as_f64().unwrap_or(0.0) as f32,
                        )
                    } else {
                        (0.0, 0.0, 0.0, 0.0)
                    }
                } else {
                    (0.0, 0.0, 0.0, 0.0)
                };

                let mut uvs = [
                    Vec2::new(u1 / res_w, v1 / res_h),
                    Vec2::new(u2 / res_w, v1 / res_h),
                    Vec2::new(u1 / res_w, v2 / res_h),
                    Vec2::new(u2 / res_w, v2 / res_h),
                ];

                if let Some(rot_val) = f_val.get("rotation").and_then(|r| r.as_i64()) {
                    let mut rot = ((rot_val % 360) + 360) % 360;
                    while rot > 0 {
                        let a = uvs[0];
                        uvs[0] = uvs[2];
                        uvs[2] = uvs[3];
                        uvs[3] = uvs[1];
                        uvs[1] = a;
                        rot -= 90;
                    }
                }

                let mut local_corners = [Vec3::default(); 4];
                for i in 0..4 {
                    let mut pt = corners[i];
                    if has_rot {
                        pt = rotate_euler_xyz(&pt, &origin, rx, ry, rz);
                    }
                    local_corners[i] = pt;
                }

                let local_norm = if has_rot {
                    rotate_normal_euler_xyz(norm, rx, ry, rz)
                } else {
                    *norm
                };

                cube_faces.push(CubeFace {
                    corners_local: local_corners,
                    normal_local: local_norm,
                    uvs,
                });
            }
        }

        let cube_uuid = el.get("uuid").and_then(|u| u.as_str()).unwrap_or("").to_string();
        let cube_name = el.get("name").and_then(|n| n.as_str()).unwrap_or("").to_string();
        let ancestors = element_ancestors.get(&cube_uuid).cloned().unwrap_or_default();

        parsed_cubes.push(ParsedCube {
            name: cube_name,
            uuid: cube_uuid,
            ancestors,
            faces: cube_faces,
        });
    }

    let mut out_vertices = Vec::new();

    // Helper closure to emit 2 triangles for a quad face matching Blockbench indices [0, 2, 1, 2, 3, 1]
    let mut emit_face = |c: [Vec3; 4], n: Vec3, uvs: [Vec2; 4]| {
        let v0 = Vertex { pos: c[0], normal: n, uv: uvs[0], part_id: 5 };
        let v1 = Vertex { pos: c[1], normal: n, uv: uvs[1], part_id: 5 };
        let v2 = Vertex { pos: c[2], normal: n, uv: uvs[2], part_id: 5 };
        let v3 = Vertex { pos: c[3], normal: n, uv: uvs[3], part_id: 5 };

        // Triangle 1: (0, 2, 1)
        out_vertices.push(v0);
        out_vertices.push(v2);
        out_vertices.push(v1);

        // Triangle 2: (2, 3, 1)
        out_vertices.push(v2);
        out_vertices.push(v3);
        out_vertices.push(v1);
    };

    // 4. If modular properties and definitions are provided, instantiate parts according to definitions
    if let (Some(props), Some(defs)) = (props_opt, defs_opt) {
        let mut defs_map: HashMap<&str, &PositionDefinition> = HashMap::new();
        for d in &defs.position_definitions {
            defs_map.insert(d.name.as_str(), d);
        }

        let _model_y_offset = props.model_y_offset;

        let cube_matches_part = |cube: &ParsedCube, part_names: &[String]| -> bool {
            part_names.iter().any(|pn| {
                let trimmed = pn.trim();
                if trimmed.is_empty() {
                    return false;
                }
                trimmed.eq_ignore_ascii_case(&cube.name)
                    || cube.ancestors.iter().any(|a| a.eq_ignore_ascii_case(trimmed))
            })
        };

        for part in &props.parts {
            if !is_condition_active(&part.condition) || is_part_skipped(&part.names) {
                continue;
            }

            for def_id in &part.position_definitions {
                let def_obj = match defs_map.get(def_id.as_str()) {
                    Some(d) => d,
                    None => continue,
                };

                if def_obj.positions.is_empty() && def_obj.positions_flipped.is_empty() {
                    continue;
                }

                for cube in &parsed_cubes {
                    if !cube_matches_part(cube, &part.names) {
                        continue;
                    }

                    // Forward positions
                    for pos in &def_obj.positions {
                        for face in &cube.faces {
                            let mut corners = [Vec3::default(); 4];
                            for i in 0..4 {
                                corners[i] = Vec3::new(
                                    (face.corners_local[i].x + pos.x) / 16.0,
                                    (face.corners_local[i].y + pos.y) / 16.0,
                                    (face.corners_local[i].z + pos.z) / 16.0,
                                );
                            }
                            emit_face(corners, face.normal_local, face.uvs);
                        }
                    }

                    // Flipped positions (rotate 180 degrees around Y axis)
                    for pos in &def_obj.positions_flipped {
                        for face in &cube.faces {
                            let mut corners = [Vec3::default(); 4];
                            for i in 0..4 {
                                corners[i] = Vec3::new(
                                    (-face.corners_local[i].x - pos.x) / 16.0,
                                    (face.corners_local[i].y + pos.y) / 16.0,
                                    (-face.corners_local[i].z + pos.z) / 16.0,
                                );
                            }
                            let norm = Vec3::new(-face.normal_local.x, face.normal_local.y, -face.normal_local.z);
                            emit_face(corners, norm, face.uvs);
                        }
                    }
                }
            }
        }
    } else {
        // Fallback: render all cubes at default scale
        for cube in &parsed_cubes {
            for face in &cube.faces {
                let mut corners = [Vec3::default(); 4];
                for i in 0..4 {
                    corners[i] = Vec3::new(
                        face.corners_local[i].x / 16.0,
                        face.corners_local[i].y / 16.0,
                        face.corners_local[i].z / 16.0,
                    );
                }
                emit_face(corners, face.normal_local, face.uvs);
            }
        }
    }

    Ok(out_vertices)
}

/// Fallback wrapper for parse_bbmodel without modular definitions
pub fn parse_bbmodel(json_str: &str) -> Result<Vec<Vertex>, String> {
    parse_bbmodel_modular(json_str, None, None)
}

/// Parse OBJ vehicle string into a 3D vertex mesh with modular part placement
pub fn parse_obj_vehicle_modular(
    obj_text: &str,
    props_opt: Option<&ModelProperties>,
    defs_opt: Option<&PositionDefinitionFile>,
) -> Result<Vec<Vertex>, String> {
    let mut raw_positions: Vec<Vec3> = Vec::new();
    let mut raw_normals: Vec<Vec3> = Vec::new();
    let mut raw_uvs: Vec<Vec2> = Vec::new();

    let mut current_group = "default".to_string();
    let mut grouped_faces: Vec<(String, Vec<Vertex>)> = Vec::new();

    for line in obj_text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }

        if line.starts_with("o ") || line.starts_with("g ") {
            let grp = line[2..].trim();
            if !grp.is_empty() {
                current_group = grp.to_string();
            }
            continue;
        }

        if line.starts_with("vt ") {
            let parts: Vec<&str> = line[3..].split_whitespace().collect();
            if parts.len() >= 2 {
                if let (Ok(u), Ok(v)) = (parts[0].parse::<f32>(), parts[1].parse::<f32>()) {
                    raw_uvs.push(Vec2::new(u, 1.0 - v));
                }
            }
            continue;
        }

        if line.starts_with("v ") {
            let parts: Vec<&str> = line[2..].split_whitespace().collect();
            if parts.len() >= 3 {
                if let (Ok(x), Ok(y), Ok(z)) = (
                    parts[0].parse::<f32>(),
                    parts[1].parse::<f32>(),
                    parts[2].parse::<f32>(),
                ) {
                    raw_positions.push(Vec3::new(x, y, z));
                }
            }
            continue;
        }

        if line.starts_with("vn ") {
            let parts: Vec<&str> = line[3..].split_whitespace().collect();
            if parts.len() >= 3 {
                if let (Ok(x), Ok(y), Ok(z)) = (
                    parts[0].parse::<f32>(),
                    parts[1].parse::<f32>(),
                    parts[2].parse::<f32>(),
                ) {
                    raw_normals.push(Vec3::new(x, y, z));
                }
            }
            continue;
        }

        if line.starts_with("f ") {
            let face_tokens: Vec<&str> = line[2..].split_whitespace().collect();
            if face_tokens.len() < 3 {
                continue;
            }

            let mut face_verts = Vec::new();
            for token in face_tokens {
                let sub_tokens: Vec<&str> = token.split('/').collect();
                let v_idx = sub_tokens[0].parse::<isize>().unwrap_or(0);
                let pos = if v_idx > 0 {
                    raw_positions.get((v_idx - 1) as usize).copied()
                } else if v_idx < 0 {
                    raw_positions
                        .get((raw_positions.len() as isize + v_idx) as usize)
                        .copied()
                } else {
                    None
                };

                let uv = if sub_tokens.len() >= 2 && !sub_tokens[1].is_empty() {
                    let vt_idx = sub_tokens[1].parse::<isize>().unwrap_or(0);
                    if vt_idx > 0 {
                        raw_uvs.get((vt_idx - 1) as usize).copied()
                    } else if vt_idx < 0 {
                        raw_uvs
                            .get((raw_uvs.len() as isize + vt_idx) as usize)
                            .copied()
                    } else {
                        None
                    }
                } else {
                    None
                };

                let norm = if sub_tokens.len() >= 3 && !sub_tokens[2].is_empty() {
                    let n_idx = sub_tokens[2].parse::<isize>().unwrap_or(0);
                    if n_idx > 0 {
                        raw_normals.get((n_idx - 1) as usize).copied()
                    } else if n_idx < 0 {
                        raw_normals
                            .get((raw_normals.len() as isize + n_idx) as usize)
                            .copied()
                    } else {
                        None
                    }
                } else {
                    None
                };

                if let Some(p) = pos {
                    face_verts.push(Vertex {
                        pos: p,
                        normal: norm.unwrap_or(Vec3::new(0.0, 1.0, 0.0)),
                        uv: uv.unwrap_or_default(),
                        part_id: 5,
                    });
                }
            }

            if face_verts.len() >= 3 {
                grouped_faces.push((current_group.clone(), face_verts));
            }
        }
    }

    let mut out_vertices = Vec::new();

    // Helper closure to triangulate a polygon
    let triangulate = |verts: &[Vertex], out: &mut Vec<Vertex>| {
        if verts.len() >= 3 {
            for i in 1..(verts.len() - 1) {
                out.push(verts[0]);
                out.push(verts[i]);
                out.push(verts[i + 1]);
            }
        }
    };

    if let (Some(props), Some(defs)) = (props_opt, defs_opt) {
        let mut defs_map: HashMap<&str, &PositionDefinition> = HashMap::new();
        for d in &defs.position_definitions {
            defs_map.insert(d.name.as_str(), d);
        }

        let _model_y_offset = props.model_y_offset;

        for part in &props.parts {
            if !is_condition_active(&part.condition) || is_part_skipped(&part.names) {
                continue;
            }

            for def_id in &part.position_definitions {
                let def_obj = match defs_map.get(def_id.as_str()) {
                    Some(d) => d,
                    None => continue,
                };

                if def_obj.positions.is_empty() && def_obj.positions_flipped.is_empty() {
                    continue;
                }

                for (grp, face_verts) in &grouped_faces {
                    let matches = part.names.iter().any(|pn| {
                        let t = pn.trim();
                        !t.is_empty() && t.eq_ignore_ascii_case(grp)
                    });

                    if !matches {
                        continue;
                    }

                    // Forward positions
                    for pos in &def_obj.positions {
                        let mut transformed: Vec<Vertex> = Vec::with_capacity(face_verts.len());
                        for v in face_verts {
                            transformed.push(Vertex {
                                pos: Vec3::new(
                                    v.pos.x + pos.x / 16.0,
                                    v.pos.y + pos.y / 16.0,
                                    v.pos.z + pos.z / 16.0,
                                ),
                                normal: v.normal,
                                uv: v.uv,
                                part_id: 5,
                            });
                        }
                        triangulate(&transformed, &mut out_vertices);
                    }

                    // Flipped positions
                    for pos in &def_obj.positions_flipped {
                        let mut transformed: Vec<Vertex> = Vec::with_capacity(face_verts.len());
                        for v in face_verts {
                            transformed.push(Vertex {
                                pos: Vec3::new(
                                    -v.pos.x + pos.x / 16.0,
                                    v.pos.y + pos.y / 16.0,
                                    -v.pos.z + pos.z / 16.0,
                                ),
                                normal: Vec3::new(-v.normal.x, v.normal.y, -v.normal.z),
                                uv: v.uv,
                                part_id: 5,
                            });
                        }
                        triangulate(&transformed, &mut out_vertices);
                    }
                }
            }
        }
    } else {
        // Fallback: triangulate all faces directly
        for (_, face_verts) in &grouped_faces {
            triangulate(face_verts, &mut out_vertices);
        }
    }

    Ok(out_vertices)
}

/// Fallback wrapper for parse_obj_vehicle without modular definitions
pub fn parse_obj_vehicle(obj_text: &str) -> Result<Vec<Vertex>, String> {
    parse_obj_vehicle_modular(obj_text, None, None)
}

/// Helper: Rotate a point around an origin with Euler angles XYZ in degrees
fn rotate_euler_xyz(pt: &Vec3, origin: &Vec3, rx_deg: f32, ry_deg: f32, rz_deg: f32) -> Vec3 {
    let mut x = pt.x - origin.x;
    let mut y = pt.y - origin.y;
    let mut z = pt.z - origin.z;

    let rx = rx_deg.to_radians();
    let ry = ry_deg.to_radians();
    let rz = rz_deg.to_radians();

    if rx_deg.abs() > 1e-4 {
        let (sin_x, cos_x) = rx.sin_cos();
        let y_new = y * cos_x - z * sin_x;
        let z_new = y * sin_x + z * cos_x;
        y = y_new;
        z = z_new;
    }

    if ry_deg.abs() > 1e-4 {
        let (sin_y, cos_y) = ry.sin_cos();
        let x_new = x * cos_y + z * sin_y;
        let z_new = -x * sin_y + z * cos_y;
        x = x_new;
        z = z_new;
    }

    if rz_deg.abs() > 1e-4 {
        let (sin_z, cos_z) = rz.sin_cos();
        let x_new = x * cos_z - y * sin_z;
        let y_new = x * sin_z + y * cos_z;
        x = x_new;
        y = y_new;
    }

    Vec3::new(x + origin.x, y + origin.y, z + origin.z)
}

/// Helper: Rotate normal vector with Euler angles XYZ in degrees
fn rotate_normal_euler_xyz(norm: &Vec3, rx_deg: f32, ry_deg: f32, rz_deg: f32) -> Vec3 {
    let mut x = norm.x;
    let mut y = norm.y;
    let mut z = norm.z;

    let rx = rx_deg.to_radians();
    let ry = ry_deg.to_radians();
    let rz = rz_deg.to_radians();

    if rx_deg.abs() > 1e-4 {
        let (sin_x, cos_x) = rx.sin_cos();
        let y_new = y * cos_x - z * sin_x;
        let z_new = y * sin_x + z * cos_x;
        y = y_new;
        z = z_new;
    }

    if ry_deg.abs() > 1e-4 {
        let (sin_y, cos_y) = ry.sin_cos();
        let x_new = x * cos_y + z * sin_y;
        let z_new = -x * sin_y + z * cos_y;
        x = x_new;
        z = z_new;
    }

    if rz_deg.abs() > 1e-4 {
        let (sin_z, cos_z) = rz.sin_cos();
        let x_new = x * cos_z - y * sin_z;
        let y_new = x * sin_z + y * cos_z;
        x = x_new;
        y = y_new;
    }

    Vec3::new(x, y, z)
}

/// Read a file from either a zip archive or a directory, resolving namespace identifiers
pub fn read_pack_file(pack_path_str: &str, resource_str: &str) -> Result<Vec<u8>, String> {
    let pack_path = Path::new(pack_path_str);
    if !pack_path.exists() {
        return Err(format!("Pack path does not exist: {}", pack_path_str));
    }

    // Convert "mtr:abb.bbmodel" -> "assets/mtr/abb.bbmodel"
    let clean_res = resource_str.trim().replace('\\', "/");
    let candidate_path = if let Some((ns, sub)) = clean_res.split_once(':') {
        format!("assets/{}/{}", ns, sub)
    } else {
        clean_res.clone()
    };

    let filename = Path::new(&clean_res)
        .file_name()
        .and_then(|f| f.to_str())
        .unwrap_or(&clean_res)
        .to_lowercase();

    if pack_path.is_file() {
        let file = File::open(pack_path).map_err(|e| format!("Cannot open zip: {}", e))?;
        let mut archive = ZipArchive::new(file).map_err(|e| format!("Cannot read zip: {}", e))?;

        if let Ok(mut zf) = archive.by_name(&candidate_path) {
            let mut buf = Vec::new();
            zf.read_to_end(&mut buf).map_err(|e| e.to_string())?;
            return Ok(buf);
        }

        if let Ok(mut zf) = archive.by_name(&clean_res) {
            let mut buf = Vec::new();
            zf.read_to_end(&mut buf).map_err(|e| e.to_string())?;
            return Ok(buf);
        }

        for i in 0..archive.len() {
            if let Ok(mut zf) = archive.by_index(i) {
                let name = zf.name().to_lowercase().replace('\\', "/");
                if name.ends_with(&filename) || name.ends_with(&candidate_path.to_lowercase()) {
                    let mut buf = Vec::new();
                    zf.read_to_end(&mut buf).map_err(|e| e.to_string())?;
                    return Ok(buf);
                }
            }
        }

        Err(format!("Resource '{}' not found in zip archive", resource_str))
    } else if pack_path.is_dir() {
        let p1 = pack_path.join(&candidate_path);
        if p1.is_file() {
            return std::fs::read(&p1).map_err(|e| e.to_string());
        }

        let p2 = pack_path.join(&clean_res);
        if p2.is_file() {
            return std::fs::read(&p2).map_err(|e| e.to_string());
        }

        for entry in walkdir::WalkDir::new(pack_path).into_iter().filter_map(|e| e.ok()) {
            if entry.file_type().is_file() {
                let fname = entry.file_name().to_string_lossy().to_lowercase();
                if fname == filename {
                    return std::fs::read(entry.path()).map_err(|e| e.to_string());
                }
            }
        }

        Err(format!("Resource '{}' not found in directory", resource_str))
    } else {
        Err("Pack is neither a file nor a directory".to_string())
    }
}

/// Helper to load a modular model (either .bbmodel or .obj) given optional properties and definition resources
fn load_single_modular_model(
    pack_path: &str,
    model_res: &str,
    props_res: Option<&str>,
    defs_res: Option<&str>,
) -> Result<Vec<Vertex>, String> {
    let bytes = read_pack_file(pack_path, model_res)?;
    let content = String::from_utf8_lossy(&bytes);

    let props_opt = if let Some(pr) = props_res.filter(|s| !s.trim().is_empty()) {
        read_pack_file(pack_path, pr)
            .ok()
            .and_then(|b| parse_properties_json(&String::from_utf8_lossy(&b)).ok())
    } else {
        None
    };

    let defs_opt = if let Some(dr) = defs_res.filter(|s| !s.trim().is_empty()) {
        read_pack_file(pack_path, dr)
            .ok()
            .and_then(|b| parse_definitions_json(&String::from_utf8_lossy(&b)).ok())
    } else {
        None
    };

    let lower_res = model_res.to_lowercase();
    if lower_res.ends_with(".bbmodel") {
        parse_bbmodel_modular(&content, props_opt.as_ref(), defs_opt.as_ref())
    } else if lower_res.ends_with(".obj") {
        parse_obj_vehicle_modular(&content, props_opt.as_ref(), defs_opt.as_ref())
    } else {
        let trimmed = content.trim_start();
        if trimmed.starts_with('{') {
            parse_bbmodel_modular(&content, props_opt.as_ref(), defs_opt.as_ref())
        } else {
            parse_obj_vehicle_modular(&content, props_opt.as_ref(), defs_opt.as_ref())
        }
    }
}

/// Load a complete modular vehicle mesh (.bbmodel or .obj) with body and bogies from train pack
pub fn load_vehicle_mesh_modular(
    pack_path: &str,
    model_resource: &str,
    props_resource: Option<&str>,
    defs_resource: Option<&str>,
    bogie1_model: Option<&str>,
    bogie1_props: Option<&str>,
    bogie1_defs: Option<&str>,
    bogie1_pos: f32,
    bogie2_model: Option<&str>,
    bogie2_props: Option<&str>,
    bogie2_defs: Option<&str>,
    bogie2_pos: f32,
) -> Result<Vec<Vertex>, String> {
    // 1. Car body
    let mut total_vertices = load_single_modular_model(pack_path, model_resource, props_resource, defs_resource)?;

    // Calculate car body bounding box in Z (with a 0.5m tolerance)
    let (min_body_z, max_body_z) = if total_vertices.is_empty() {
        (-100.0, 100.0)
    } else {
        total_vertices.iter().fold((f32::INFINITY, f32::NEG_INFINITY), |(min_z, max_z), v| {
            (min_z.min(v.pos.z), max_z.max(v.pos.z))
        })
    };

    // 2. Bogie 1
    if let Some(b1_model) = bogie1_model.filter(|s| !s.trim().is_empty()) {
        if bogie1_pos >= min_body_z - 0.5 && bogie1_pos <= max_body_z + 0.5 {
            if let Ok(mut b1_verts) = load_single_modular_model(pack_path, b1_model, bogie1_props, bogie1_defs) {
                for v in &mut b1_verts {
                    v.pos.z += bogie1_pos;
                }
                total_vertices.extend(b1_verts);
            }
        }
    }

    // 3. Bogie 2
    if let Some(b2_model) = bogie2_model.filter(|s| !s.trim().is_empty()) {
        if bogie2_pos >= min_body_z - 0.5 && bogie2_pos <= max_body_z + 0.5 {
            if let Ok(mut b2_verts) = load_single_modular_model(pack_path, b2_model, bogie2_props, bogie2_defs) {
                for v in &mut b2_verts {
                    v.pos.z += bogie2_pos;
                }
                total_vertices.extend(b2_verts);
            }
        }
    }

    Ok(total_vertices)
}

/// Load a vehicle mesh (.bbmodel or .obj) directly from the train pack
pub fn load_vehicle_mesh_from_pack(pack_path: &str, model_resource: &str) -> Result<Vec<Vertex>, String> {
    load_vehicle_mesh_modular(
        pack_path,
        model_resource,
        None,
        None,
        None,
        None,
        None,
        0.0,
        None,
        None,
        None,
        0.0,
    )
}
