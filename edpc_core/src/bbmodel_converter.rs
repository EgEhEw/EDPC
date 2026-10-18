use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};

const SCALE: f32 = 16.0;

fn fmt(n: f32) -> String {
    if n.abs() < 1e-6 {
        return "0".to_string();
    }
    let s = format!("{:.5}", n);
    s.trim_end_matches('0').trim_end_matches('.').to_string()
}

fn decode_base64(input: &str) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    let mut buf: u32 = 0;
    let mut bits: u32 = 0;
    for &b in input.as_bytes() {
        let val = match b {
            b'A'..=b'Z' => (b - b'A') as u32,
            b'a'..=b'z' => (b - b'a' + 26) as u32,
            b'0'..=b'9' => (b - b'0' + 52) as u32,
            b'+' => 62,
            b'/' => 63,
            b'=' | b'\r' | b'\n' | b' ' => continue,
            _ => return None,
        };
        buf = (buf << 6) | val;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((buf >> bits) as u8);
            buf &= (1 << bits) - 1;
        }
    }
    Some(out)
}

fn rotate_point(p: [f32; 3], origin: [f32; 3], rot_deg: [f32; 3]) -> [f32; 3] {
    let mut x = p[0] - origin[0];
    let mut y = p[1] - origin[1];
    let mut z = p[2] - origin[2];

    if rot_deg[0].abs() > 1e-5 {
        let (s, c) = rot_deg[0].to_radians().sin_cos();
        let ny = y * c - z * s;
        let nz = y * s + z * c;
        y = ny;
        z = nz;
    }
    if rot_deg[1].abs() > 1e-5 {
        let (s, c) = rot_deg[1].to_radians().sin_cos();
        let nx = x * c + z * s;
        let nz = -x * s + z * c;
        x = nx;
        z = nz;
    }
    if rot_deg[2].abs() > 1e-5 {
        let (s, c) = rot_deg[2].to_radians().sin_cos();
        let nx = x * c - y * s;
        let ny = x * s + y * c;
        x = nx;
        y = ny;
    }
    [x + origin[0], y + origin[1], z + origin[2]]
}

fn normalize_group_name(raw: &str) -> String {
    let s = raw.trim().to_lowercase();
    if s == "p_base" || s == "base" || s.starts_with("p_base") || s.starts_with("base") {
        "p_base".to_string()
    } else if s == "p1" || s.starts_with("p1") {
        "p1".to_string()
    } else if s == "p2" || s.starts_with("p2") {
        "p2".to_string()
    } else if s == "p3" || s.starts_with("p3") {
        "p3".to_string()
    } else if s == "p4" || s.starts_with("p4") {
        "p4".to_string()
    } else {
        s
    }
}

pub fn calculate_pivots_from_bbmodel(root: &serde_json::Value) -> Vec<String> {
    let mut origin_by_group: HashMap<String, [f32; 3]> = HashMap::new();
    let mut p4_group_val: Option<&serde_json::Value> = None;

    if let Some(groups) = root.get("groups").and_then(|g| g.as_array()) {
        for g in groups {
            if let Some(name) = g.get("name").and_then(|n| n.as_str()) {
                if let Some(orig_arr) = g.get("origin").and_then(|o| o.as_array()) {
                    if orig_arr.len() >= 3 {
                        let x = orig_arr[0].as_f64().unwrap_or(0.0) as f32;
                        let y = orig_arr[1].as_f64().unwrap_or(0.0) as f32;
                        let z = orig_arr[2].as_f64().unwrap_or(0.0) as f32;
                        origin_by_group.insert(name.to_string(), [x, y, z]);
                    }
                }
                if name == "p4" {
                    p4_group_val = Some(g);
                }
            }
        }
    }

    let mut lines = Vec::new();

    if let Some(p1) = origin_by_group.get("p1") {
        lines.push(format!("# pivot p1 0 {} {}", fmt(p1[1] / SCALE), fmt(p1[2] / SCALE)));
    }
    if let Some(p2) = origin_by_group.get("p2") {
        lines.push(format!("# pivot p2 0 {} {}", fmt(p2[1] / SCALE), fmt(p2[2] / SCALE)));
    }
    if let Some(p3) = origin_by_group.get("p3") {
        lines.push(format!("# pivot p3 0 {} {}", fmt(p3[1] / SCALE), fmt(p3[2] / SCALE)));
    }

    // p4 coupler rod
    if let Some(p4_grp) = p4_group_val {
        if let (Some(p4_orig), Some(p2_orig)) = (origin_by_group.get("p4"), origin_by_group.get("p2")) {
            let mut p4_uuids = HashSet::new();
            fn collect_all(node: &serde_json::Value, uuids: &mut HashSet<String>) {
                if let Some(u) = node.as_str() {
                    uuids.insert(u.to_string());
                } else if let Some(children) = node.get("children").and_then(|c| c.as_array()) {
                    for c in children {
                        collect_all(c, uuids);
                    }
                }
            }

            fn find_p4_and_collect(node: &serde_json::Value, uuids: &mut HashSet<String>, target_uuid: Option<&str>) {
                if let Some(obj) = node.as_object() {
                    let is_target = obj.get("name").and_then(|n| n.as_str()) == Some("p4")
                        || (target_uuid.is_some() && obj.get("uuid").and_then(|u| u.as_str()) == target_uuid);
                    if is_target {
                        collect_all(node, uuids);
                    } else if let Some(children) = obj.get("children").and_then(|c| c.as_array()) {
                        for c in children {
                            find_p4_and_collect(c, uuids, target_uuid);
                        }
                    }
                }
            }

            let p4_uuid = p4_grp.get("uuid").and_then(|u| u.as_str());
            if let Some(outliner) = root.get("outliner").and_then(|o| o.as_array()) {
                for node in outliner {
                    find_p4_and_collect(node, &mut p4_uuids, p4_uuid);
                }
            }
            if let Some(children) = p4_grp.get("children").and_then(|c| c.as_array()) {
                for c in children {
                    if let Some(u) = c.as_str() {
                        p4_uuids.insert(u.to_string());
                    }
                }
            }

            let mut p4_verts = Vec::new();
            if let Some(elements) = root.get("elements").and_then(|e| e.as_array()) {
                for el in elements {
                    if let Some(uuid) = el.get("uuid").and_then(|u| u.as_str()) {
                        if p4_uuids.contains(uuid) {
                            if let (Some(f), Some(t)) = (
                                el.get("from").and_then(|f| f.as_array()),
                                el.get("to").and_then(|t| t.as_array()),
                            ) {
                                if f.len() >= 3 && t.len() >= 3 {
                                    let x1 = f[0].as_f64().unwrap_or(0.0) as f32;
                                    let y1 = f[1].as_f64().unwrap_or(0.0) as f32;
                                    let z1 = f[2].as_f64().unwrap_or(0.0) as f32;
                                    let x2 = t[0].as_f64().unwrap_or(0.0) as f32;
                                    let y2 = t[1].as_f64().unwrap_or(0.0) as f32;
                                    let z2 = t[2].as_f64().unwrap_or(0.0) as f32;

                                    let orig = if let Some(oa) = el.get("origin").and_then(|o| o.as_array()) {
                                        [oa[0].as_f64().unwrap_or(0.0) as f32, oa[1].as_f64().unwrap_or(0.0) as f32, oa[2].as_f64().unwrap_or(0.0) as f32]
                                    } else {
                                        [0.0, 0.0, 0.0]
                                    };
                                    let rot = if let Some(ra) = el.get("rotation").and_then(|r| r.as_array()) {
                                        [ra[0].as_f64().unwrap_or(0.0) as f32, ra[1].as_f64().unwrap_or(0.0) as f32, ra[2].as_f64().unwrap_or(0.0) as f32]
                                    } else {
                                        [0.0, 0.0, 0.0]
                                    };

                                    let corners = [
                                        [x1, y1, z1], [x2, y1, z1], [x1, y2, z1], [x2, y2, z1],
                                        [x1, y1, z2], [x2, y1, z2], [x1, y2, z2], [x2, y2, z2],
                                    ];
                                    for c in corners {
                                        p4_verts.push(rotate_point(c, orig, rot));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            let end_a = *p4_orig;
            if !p4_verts.is_empty() {
                p4_verts.sort_by(|a, b| {
                    let da = (a[1] - end_a[1]).hypot(a[2] - end_a[2]);
                    let db = (b[1] - end_a[1]).hypot(b[2] - end_a[2]);
                    db.partial_cmp(&da).unwrap_or(std::cmp::Ordering::Equal)
                });

                let take_count = p4_verts.len().min(8);
                let furthest = &p4_verts[..take_count];
                let mut sum_y = 0.0;
                let mut sum_z = 0.0;
                for p in furthest {
                    sum_y += p[1];
                    sum_z += p[2];
                }
                let end_b = [0.0, sum_y / take_count as f32, sum_z / take_count as f32];

                let dist_a = (end_a[1] - p2_orig[1]).hypot(end_a[2] - p2_orig[2]);
                let dist_b = (end_b[1] - p2_orig[1]).hypot(end_b[2] - p2_orig[2]);

                let (p4_elbow, p4_base) = if dist_a < dist_b {
                    (end_a, end_b)
                } else {
                    (end_b, end_a)
                };

                lines.push(format!("# pivot p4 0 {} {}", fmt(p4_base[1] / SCALE), fmt(p4_base[2] / SCALE)));
                lines.push(format!("# pivot p4_elbow 0 {} {}", fmt(p4_elbow[1] / SCALE), fmt(p4_elbow[2] / SCALE)));
            }
        }
    }

    lines
}

pub fn convert_or_fix_bbmodel(bbmodel_path_str: &str) -> Result<String, String> {
    let bb_path = Path::new(bbmodel_path_str);
    if !bb_path.exists() {
        return Err(format!("File does not exist: {}", bbmodel_path_str));
    }

    let dir = bb_path.parent().unwrap_or_else(|| Path::new("."));
    let stem = bb_path.file_stem().and_then(|s| s.to_str()).unwrap_or("model");

    let json_text = fs::read_to_string(bb_path)
        .map_err(|e| format!("Failed to read .bbmodel: {}", e))?;
    let root: serde_json::Value = serde_json::from_str(&json_text)
        .map_err(|e| format!("Failed to parse JSON: {}", e))?;

    // 1. Extract texture & MTL
    let mut tex_filename_opt = None;
    if let Some(textures) = root.get("textures").and_then(|t| t.as_array()) {
        for t in textures {
            let name = t.get("name").and_then(|n| n.as_str()).unwrap_or("panto.png");
            let clean_name = if name.ends_with(".png") {
                name.to_string()
            } else {
                format!("{}.png", name)
            };

            if let Some(src) = t.get("source").and_then(|s| s.as_str()) {
                if let Some(idx) = src.find(";base64,") {
                    let b64_str = &src[idx + 8..];
                    if let Some(bytes) = decode_base64(b64_str) {
                        let out_tex_path = dir.join(&clean_name);
                        if !out_tex_path.exists() {
                            let _ = fs::write(&out_tex_path, &bytes);
                        }
                        tex_filename_opt = Some(clean_name);
                        break;
                    }
                }
            }
            if tex_filename_opt.is_none() {
                tex_filename_opt = Some(clean_name);
            }
        }
    }

    let tex_filename = tex_filename_opt.unwrap_or_else(|| "panto.png".to_string());
    let mtl_filename = format!("{}.mtl", stem);
    let mtl_path = dir.join(&mtl_filename);
    if !mtl_path.exists() {
        let mtl_content = format!(
            "# Generated by EDPC\nnewmtl panto\nmap_Kd {}\n",
            tex_filename
        );
        let _ = fs::write(&mtl_path, mtl_content);
    }

    // 2. Compute pivots
    let pivot_headers = calculate_pivots_from_bbmodel(&root);

    // 3. Map elements to root groups
    let mut group_name_by_uuid: HashMap<String, String> = HashMap::new();
    if let Some(groups) = root.get("groups").and_then(|g| g.as_array()) {
        for g in groups {
            if let (Some(uuid), Some(name)) = (
                g.get("uuid").and_then(|u| u.as_str()),
                g.get("name").and_then(|n| n.as_str()),
            ) {
                group_name_by_uuid.insert(uuid.to_string(), normalize_group_name(name));
            }
        }
    }

    let mut element_to_root_group: HashMap<String, String> = HashMap::new();
    fn map_to_root(
        node: &serde_json::Value,
        root_name: Option<String>,
        groups_by_uuid: &HashMap<String, String>,
        out_map: &mut HashMap<String, String>,
    ) {
        if let Some(uuid_str) = node.as_str() {
            if let Some(rn) = root_name {
                out_map.insert(uuid_str.to_string(), rn);
            }
        } else if let Some(obj) = node.as_object() {
            let g_name = if let Some(rn) = root_name {
                rn
            } else if let Some(n) = obj.get("name").and_then(|n| n.as_str()) {
                normalize_group_name(n)
            } else if let Some(uuid) = obj.get("uuid").and_then(|u| u.as_str()) {
                groups_by_uuid.get(uuid).cloned().unwrap_or_else(|| "default".to_string())
            } else {
                "default".to_string()
            };

            if let Some(children) = obj.get("children").and_then(|c| c.as_array()) {
                for c in children {
                    map_to_root(c, Some(g_name.clone()), groups_by_uuid, out_map);
                }
            }
        }
    }

    if let Some(outliner) = root.get("outliner").and_then(|o| o.as_array()) {
        for node in outliner {
            map_to_root(node, None, &group_name_by_uuid, &mut element_to_root_group);
        }
    }
    // Fallback through bb.groups
    if let Some(groups) = root.get("groups").and_then(|g| g.as_array()) {
        for g in groups {
            let g_name = g.get("name").and_then(|n| n.as_str()).map(normalize_group_name).unwrap_or_else(|| "default".to_string());
            if let Some(children) = g.get("children").and_then(|c| c.as_array()) {
                for c in children {
                    if let Some(u) = c.as_str() {
                        element_to_root_group.entry(u.to_string()).or_insert_with(|| g_name.clone());
                    }
                }
            }
        }
    }

    // 4. Check if an OBJ file already exists in same dir (exported from Blockbench)
    let candidate_obj_names = [
        format!("{}.obj", stem),
        format!("{}.obj", stem.replace('-', "_")),
        format!("{}.obj", stem.replace('_', "-")),
    ];

    let mut existing_obj_path: Option<PathBuf> = None;
    for cand in &candidate_obj_names {
        let p = dir.join(cand);
        if p.exists() {
            existing_obj_path = Some(p);
            break;
        }
    }

    let target_obj_path = dir.join(format!("{}.obj", stem));

    if let Some(ref orig_path) = existing_obj_path {
        // Fix existing OBJ file by injecting g <group> and # pivot headers
        if let Ok(fixed_content) = fix_existing_obj_content(orig_path, &root, &element_to_root_group, &pivot_headers) {
            let _ = fs::write(&target_obj_path, fixed_content);
            return Ok(target_obj_path.to_string_lossy().to_string());
        }
    }

    // If no existing OBJ or fixing failed, generate a complete OBJ directly from bbmodel
    let generated = generate_obj_from_bbmodel(&root, &mtl_filename, &pivot_headers, &element_to_root_group)?;
    fs::write(&target_obj_path, generated)
        .map_err(|e| format!("Failed to write generated OBJ to {}: {}", target_obj_path.display(), e))?;

    Ok(target_obj_path.to_string_lossy().to_string())
}

fn fix_existing_obj_content(
    orig_obj_path: &Path,
    root: &serde_json::Value,
    element_to_root_group: &HashMap<String, String>,
    pivot_headers: &[String],
) -> Result<String, String> {
    let raw_text = fs::read_to_string(orig_obj_path)
        .map_err(|e| format!("Failed to read existing OBJ: {}", e))?;

    // Elements multiset per root group
    let mut elements_by_root_group: HashMap<String, Vec<String>> = HashMap::new();
    if let Some(elements) = root.get("elements").and_then(|e| e.as_array()) {
        for el in elements {
            if let (Some(uuid), Some(name)) = (
                el.get("uuid").and_then(|u| u.as_str()),
                el.get("name").and_then(|n| n.as_str()),
            ) {
                let rg = element_to_root_group.get(uuid).cloned().unwrap_or_else(|| "p_base".to_string());
                elements_by_root_group.entry(rg).or_default().push(name.to_string());
            }
        }
    }

    let mut clean_lines: Vec<String> = Vec::new();
    struct OEntry {
        line_idx: usize,
        name: String,
    }
    let mut o_entries: Vec<OEntry> = Vec::new();

    for line in raw_text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("g ") || trimmed == "g" || trimmed.starts_with("# pivot ") || trimmed.starts_with("# PANTOGRAPH PIVOT DATA") {
            continue;
        }
        let line_idx = clean_lines.len();
        clean_lines.push(line.to_string());

        if trimmed.starts_with("o ") {
            let name = trimmed[2..].trim().to_string();
            o_entries.push(OEntry { line_idx, name });
        }
    }

    // Match blocks to root groups
    let mut group_multisets: HashMap<String, HashMap<String, usize>> = HashMap::new();
    for (rg, names) in &elements_by_root_group {
        let mut m = HashMap::new();
        for n in names {
            *m.entry(n.clone()).or_insert(0) += 1;
        }
        group_multisets.insert(rg.clone(), m);
    }

    struct MatchedBlock {
        group: String,
        line_idx: usize,
    }
    let mut matched_blocks: Vec<MatchedBlock> = Vec::new();
    let mut cursor = 0;

    while cursor < o_entries.len() {
        let mut matched = false;
        // Strategy A: exact multiset match
        for (g_name, mset) in &group_multisets {
            let size = elements_by_root_group.get(g_name).map(|v| v.len()).unwrap_or(0);
            if size > 0 && cursor + size <= o_entries.len() {
                let mut candidate_mset = HashMap::new();
                for k in cursor..(cursor + size) {
                    *candidate_mset.entry(o_entries[k].name.clone()).or_insert(0) += 1;
                }
                if &candidate_mset == mset {
                    matched_blocks.push(MatchedBlock {
                        group: g_name.clone(),
                        line_idx: o_entries[cursor].line_idx,
                    });
                    cursor += size;
                    matched = true;
                    break;
                }
            }
        }
        if matched {
            continue;
        }

        // Strategy B: check if currentName belongs exclusively to one group
        let cur_name = &o_entries[cursor].name;
        let mut candidates = Vec::new();
        for (g_name, mset) in &group_multisets {
            if mset.contains_key(cur_name) {
                candidates.push(g_name.clone());
            }
        }
        if candidates.len() == 1 {
            let g_name = candidates.remove(0);
            matched_blocks.push(MatchedBlock {
                group: g_name,
                line_idx: o_entries[cursor].line_idx,
            });
        }
        cursor += 1;
    }

    // Insert 'g <group>' headers in reverse order
    matched_blocks.sort_by(|a, b| b.line_idx.cmp(&a.line_idx));
    for b in matched_blocks {
        clean_lines.insert(b.line_idx, format!("g {}", b.group));
    }

    // Insert pivot metadata after mtllib or top comments
    if !pivot_headers.is_empty() {
        let mut target_idx = 0;
        for (i, line) in clean_lines.iter().enumerate() {
            if line.starts_with("mtllib ") || line.starts_with("# Made in Blockbench") {
                target_idx = i + 1;
            }
        }
        clean_lines.insert(target_idx, "".to_string());
        clean_lines.insert(target_idx + 1, "# PANTOGRAPH PIVOT DATA (auto-generated by EDPC)".to_string());
        let mut offset = 2;
        for h in pivot_headers {
            clean_lines.insert(target_idx + offset, h.clone());
            offset += 1;
        }
        clean_lines.insert(target_idx + offset, "".to_string());
    }

    Ok(clean_lines.join("\n"))
}

fn generate_obj_from_bbmodel(
    root: &serde_json::Value,
    mtl_filename: &str,
    pivot_headers: &[String],
    element_to_root_group: &HashMap<String, String>,
) -> Result<String, String> {
    let res_w = root
        .get("resolution")
        .and_then(|r| r.get("width"))
        .and_then(|w| w.as_f64())
        .unwrap_or(512.0) as f32;
    let res_h = root
        .get("resolution")
        .and_then(|r| r.get("height"))
        .and_then(|h| h.as_f64())
        .unwrap_or(512.0) as f32;

    let elements = root
        .get("elements")
        .and_then(|e| e.as_array())
        .ok_or_else(|| "Missing 'elements' array in .bbmodel".to_string())?;

    // Group elements by root group
    let mut elements_by_group: HashMap<String, Vec<&serde_json::Value>> = HashMap::new();
    for el in elements {
        if el.get("export").and_then(|x| x.as_bool()) == Some(false) {
            continue;
        }
        let uuid = el.get("uuid").and_then(|u| u.as_str()).unwrap_or("");
        let rg = element_to_root_group.get(uuid).cloned().unwrap_or_else(|| "p_base".to_string());
        elements_by_group.entry(rg).or_default().push(el);
    }

    let mut out = String::with_capacity(65536);
    out.push_str("# Made in Blockbench (Converted directly by EDPC)\n");
    out.push_str(&format!("mtllib {}\n\n", mtl_filename));
    out.push_str("# PANTOGRAPH PIVOT DATA (auto-generated by EDPC)\n");
    for h in pivot_headers {
        out.push_str(h);
        out.push('\n');
    }
    out.push('\n');

    let mut v_index: usize = 1;
    let mut vt_index: usize = 1;
    let mut vn_index: usize = 1;

    // Enforce canonical order: p_base, p1, p2, p3, p4, then other
    let mut ordered_groups: Vec<String> = vec![
        "p_base".to_string(),
        "p1".to_string(),
        "p2".to_string(),
        "p3".to_string(),
        "p4".to_string(),
    ];
    for g in elements_by_group.keys() {
        if !ordered_groups.contains(g) {
            ordered_groups.push(g.clone());
        }
    }

    for group_name in ordered_groups {
        let el_list = match elements_by_group.get(&group_name) {
            Some(l) if !l.is_empty() => l,
            _ => continue,
        };

        out.push_str(&format!("g {}\n", group_name));

        for el in el_list {
            let el_name = el.get("name").and_then(|n| n.as_str()).unwrap_or("cube");
            out.push_str(&format!("o {}\n", el_name));

            let (Some(f), Some(t)) = (
                el.get("from").and_then(|f| f.as_array()),
                el.get("to").and_then(|t| t.as_array()),
            ) else {
                continue;
            };
            if f.len() < 3 || t.len() < 3 {
                continue;
            }

            let x1 = f[0].as_f64().unwrap_or(0.0) as f32;
            let y1 = f[1].as_f64().unwrap_or(0.0) as f32;
            let z1 = f[2].as_f64().unwrap_or(0.0) as f32;
            let x2 = t[0].as_f64().unwrap_or(0.0) as f32;
            let y2 = t[1].as_f64().unwrap_or(0.0) as f32;
            let z2 = t[2].as_f64().unwrap_or(0.0) as f32;

            let x_min = x1.min(x2);
            let x_max = x1.max(x2);
            let y_min = y1.min(y2);
            let y_max = y1.max(y2);
            let z_min = z1.min(z2);
            let z_max = z1.max(z2);

            let orig = if let Some(oa) = el.get("origin").and_then(|o| o.as_array()) {
                [oa[0].as_f64().unwrap_or(0.0) as f32, oa[1].as_f64().unwrap_or(0.0) as f32, oa[2].as_f64().unwrap_or(0.0) as f32]
            } else {
                [0.0, 0.0, 0.0]
            };
            let rot = if let Some(ra) = el.get("rotation").and_then(|r| r.as_array()) {
                [ra[0].as_f64().unwrap_or(0.0) as f32, ra[1].as_f64().unwrap_or(0.0) as f32, ra[2].as_f64().unwrap_or(0.0) as f32]
            } else {
                [0.0, 0.0, 0.0]
            };

            let faces = match el.get("faces").and_then(|f| f.as_object()) {
                Some(f) => f,
                None => continue,
            };

            // Blockbench face corner definition
            let face_defs: [(&str, [f32; 3], [[f32; 3]; 4]); 6] = [
                ("north", [0.0, 0.0, -1.0], [
                    [x_max, y_max, z_min], [x_min, y_max, z_min], [x_min, y_min, z_min], [x_max, y_min, z_min]
                ]),
                ("south", [0.0, 0.0, 1.0], [
                    [x_min, y_max, z_max], [x_max, y_max, z_max], [x_max, y_min, z_max], [x_min, y_min, z_max]
                ]),
                ("east", [1.0, 0.0, 0.0], [
                    [x_max, y_max, z_max], [x_max, y_max, z_min], [x_max, y_min, z_min], [x_max, y_min, z_max]
                ]),
                ("west", [-1.0, 0.0, 0.0], [
                    [x_min, y_max, z_min], [x_min, y_max, z_max], [x_min, y_min, z_max], [x_min, y_min, z_min]
                ]),
                ("up", [0.0, 1.0, 0.0], [
                    [x_min, y_max, z_min], [x_max, y_max, z_min], [x_max, y_max, z_max], [x_min, y_max, z_max]
                ]),
                ("down", [0.0, -1.0, 0.0], [
                    [x_min, y_min, z_max], [x_max, y_min, z_max], [x_max, y_min, z_min], [x_min, y_min, z_min]
                ]),
            ];

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
                        [u1 / res_w, 1.0 - (v1 / res_h)],
                        [u2 / res_w, 1.0 - (v1 / res_h)],
                        [u2 / res_w, 1.0 - (v2 / res_h)],
                        [u1 / res_w, 1.0 - (v2 / res_h)],
                    ];

                    if let Some(rot_val) = f_val.get("rotation").and_then(|r| r.as_i64()) {
                        let mut rot = ((rot_val % 360) + 360) % 360;
                        while rot > 0 {
                            let a = uvs[0];
                            uvs[0] = uvs[3];
                            uvs[3] = uvs[2];
                            uvs[2] = uvs[1];
                            uvs[1] = a;
                            rot -= 90;
                        }
                    }

                    // Emit rotated vertices
                    let v_start = v_index;
                    for c in corners {
                        let rc = rotate_point(*c, orig, rot);
                        out.push_str(&format!("v {} {} {}\n", fmt(rc[0] / SCALE), fmt(rc[1] / SCALE), fmt(rc[2] / SCALE)));
                        v_index += 1;
                    }

                    // Emit UVs
                    let vt_start = vt_index;
                    for uv in &uvs {
                        out.push_str(&format!("vt {} {}\n", fmt(uv[0]), fmt(uv[1])));
                        vt_index += 1;
                    }

                    // Emit normal
                    let r_norm = rotate_point(*norm, [0.0, 0.0, 0.0], rot);
                    out.push_str(&format!("vn {} {} {}\n", fmt(r_norm[0]), fmt(r_norm[1]), fmt(r_norm[2])));
                    let cur_vn = vn_index;
                    vn_index += 1;

                    // Emit quad as two triangles
                    out.push_str(&format!(
                        "f {}/{}/{} {}/{}/{} {}/{}/{}\n",
                        v_start, vt_start, cur_vn,
                        v_start + 1, vt_start + 1, cur_vn,
                        v_start + 2, vt_start + 2, cur_vn
                    ));
                    out.push_str(&format!(
                        "f {}/{}/{} {}/{}/{} {}/{}/{}\n",
                        v_start, vt_start, cur_vn,
                        v_start + 2, vt_start + 2, cur_vn,
                        v_start + 3, vt_start + 3, cur_vn
                    ));
                }
            }
        }
    }

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_calculate_pivots_abb_panto() {
        let path = "/home/ege/Genel/EgEhE's trainthings/scripts/tools/abb-panto.bbmodel";
        if !std::path::Path::new(path).exists() {
            return;
        }
        let json_text = std::fs::read_to_string(path).unwrap();
        let root: serde_json::Value = serde_json::from_str(&json_text).unwrap();
        let pivots = calculate_pivots_from_bbmodel(&root);
        println!("Calculated pivots:\n{:#?}", pivots);

        assert_eq!(pivots[0], "# pivot p1 0 0.18357 0.11706");
        assert_eq!(pivots[1], "# pivot p2 0 0.83591 -1.21875");
        assert_eq!(pivots[2], "# pivot p3 0 1.42328 0.0875");
        assert_eq!(pivots[3], "# pivot p4 0 0.2156 -0.32557");
        assert_eq!(pivots[4], "# pivot p4_elbow 0 0.75445 -1.37158");
    }
}
