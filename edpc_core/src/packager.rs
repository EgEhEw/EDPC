use crate::script_generator::ScriptConfig;
use std::collections::HashSet;
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use zip::write::SimpleFileOptions;
use zip::{ZipArchive, ZipWriter};

#[derive(Debug, Clone)]
pub struct PantographPackageItem {
    pub id: String,
    pub random_id: String,
    pub model_file_name: String,
    pub model_display_name: String,
    pub panto_obj_path: String,
    pub panto_mtl_path: Option<String>,
    pub panto_texture_path: Option<String>,
    pub script_config: ScriptConfig,
}

pub struct PackageOptions {
    pub pack_input_path: String,
    pub pack_output_path: String,
    pub panto_obj_path: String,
    pub panto_mtl_path: Option<String>,
    pub panto_texture_path: Option<String>,
    pub custom_resources_rel_path: String,
    pub script_config: ScriptConfig,
    pub pantographs: Vec<PantographPackageItem>,
}

pub fn package_train_pack(opts: &PackageOptions) -> Result<String, String> {
    let in_path = Path::new(&opts.pack_input_path);
    if !in_path.exists() {
        return Err(format!("Input pack does not exist: {}", in_path.display()));
    }

    if in_path.is_file() && in_path.extension().map_or(false, |ext| ext == "zip") {
        package_zip_pack(opts)
    } else if in_path.is_dir() {
        package_dir_pack(opts)
    } else {
        Err("Unsupported pack format. Select a .zip archive or resource pack folder.".to_string())
    }
}

pub struct ActivePantographEntry {
    pub script_id: String,
    pub script_location: String,
    pub vehicle_ids: HashSet<String>,
}

struct PreparedPantoAsset {
    script_id: String,
    script_zip_path: String,
    script_js_content: String,
    panto_obj_zip_path: String,
    panto_obj_bytes: Vec<u8>,
    mtl_zip_path: Option<String>,
    mtl_bytes: Option<Vec<u8>>,
    tex_zip_path: Option<String>,
    tex_bytes: Option<Vec<u8>>,
    vehicle_ids: HashSet<String>,
}

fn prepare_panto_obj(obj_path: &str, mtl_name: Option<&str>) -> Result<Vec<u8>, String> {
    let raw_bytes = std::fs::read(obj_path)
        .map_err(|e| format!("Failed to read pantograph OBJ file: {}", e))?;
    let Some(mtl) = mtl_name else {
        return Ok(raw_bytes);
    };
    if let Ok(content) = String::from_utf8(raw_bytes.clone()) {
        let mut lines = Vec::new();
        let mut had_mtllib = false;
        let mut had_usemtl = false;
        for line in content.lines() {
            let trimmed = line.trim_start();
            if trimmed.starts_with("usemtl ") {
                had_usemtl = true;
            }
            if trimmed.starts_with("mtllib ") {
                lines.push(format!("mtllib {}", mtl));
                had_mtllib = true;
            } else {
                lines.push(line.to_string());
            }
        }
        if !had_mtllib {
            lines.insert(0, format!("mtllib {}", mtl));
        }
        if !had_usemtl {
            let mut fixed_lines = Vec::with_capacity(lines.len() + 16);
            for l in lines {
                let is_g = l.trim_start().starts_with("g ");
                fixed_lines.push(l);
                if is_g {
                    fixed_lines.push("usemtl panto".to_string());
                }
            }
            return Ok(fixed_lines.join("\n").into_bytes());
        }
        Ok(lines.join("\n").into_bytes())
    } else {
        Ok(raw_bytes)
    }
}

fn prepare_panto_mtl(mtl_path: &str, tex_name: Option<&str>, obj_bytes: Option<&[u8]>) -> Result<Vec<u8>, String> {
    let raw_bytes = std::fs::read(mtl_path)
        .map_err(|e| format!("Failed to read pantograph MTL file: {}", e))?;
    let Some(tex) = tex_name else {
        return Ok(raw_bytes);
    };
    if let Ok(content) = String::from_utf8(raw_bytes.clone()) {
        let mut lines = Vec::new();
        let mut defined_materials = HashSet::new();
        for line in content.lines() {
            let trimmed = line.trim_start();
            if trimmed.starts_with("newmtl ") {
                let m = trimmed[7..].trim();
                if !m.is_empty() {
                    defined_materials.insert(m.to_string());
                }
                lines.push(line.to_string());
            } else if trimmed.starts_with("map_Kd ") {
                lines.push(format!("map_Kd {}", tex));
            } else {
                lines.push(line.to_string());
            }
        }

        // Collect all usemtl materials used in the OBJ file
        let mut used_materials = HashSet::new();
        if let Some(ob) = obj_bytes {
            if let Ok(obj_str) = std::str::from_utf8(ob) {
                for line in obj_str.lines() {
                    let trimmed = line.trim_start();
                    if trimmed.starts_with("usemtl ") {
                        let m = trimmed[7..].trim();
                        if !m.is_empty() && m != "none" {
                            used_materials.insert(m.to_string());
                        }
                    }
                }
            }
        }

        // Ensure "panto" is defined
        if !defined_materials.contains("panto") {
            lines.push("newmtl panto".to_string());
            lines.push(format!("map_Kd {}", tex));
            defined_materials.insert("panto".to_string());
        }

        // Ensure every material in the OBJ is defined
        for mat in used_materials {
            if !defined_materials.contains(&mat) {
                lines.push(format!("newmtl {}", mat));
                lines.push(format!("map_Kd {}", tex));
                defined_materials.insert(mat);
            }
        }

        lines.push("newmtl none".to_string());
        Ok(lines.join("\n").into_bytes())
    } else {
        Ok(raw_bytes)
    }
}

fn prepare_active_pantographs(opts: &PackageOptions) -> Result<Vec<PreparedPantoAsset>, String> {
    let panto_items: Vec<PantographPackageItem> = if !opts.pantographs.is_empty() {
        opts.pantographs.clone()
    } else {
        vec![PantographPackageItem {
            id: "panto_0".to_string(),
            random_id: opts.script_config.random_id.clone(),
            model_file_name: opts.script_config.model_file_name.clone(),
            model_display_name: opts.script_config.model_display_name.clone(),
            panto_obj_path: opts.panto_obj_path.clone(),
            panto_mtl_path: opts.panto_mtl_path.clone(),
            panto_texture_path: opts.panto_texture_path.clone(),
            script_config: opts.script_config.clone(),
        }]
    };

    let all_vehicles = &opts.script_config.vehicles;
    let mut prepared = Vec::new();

    for p in &panto_items {
        // Collect vehicles assigned to this pantograph
        let assigned: Vec<crate::pack_inspector::VehicleInfo> = if panto_items.len() == 1 {
            all_vehicles.iter().filter(|v| v.has_pantograph).cloned().collect()
        } else {
            all_vehicles
                .iter()
                .filter(|v| {
                    v.has_pantograph
                        && (v.pantograph_id.as_deref() == Some(&p.id)
                            || v.pantograph_id.as_deref() == Some(&p.random_id))
                })
                .cloned()
                .collect()
        };

        if assigned.is_empty() {
            continue; // Skip pantographs not mounted on any vehicle
        }

        let mut cfg = p.script_config.clone();
        cfg.vehicles = assigned.clone();
        let script_js_content = cfg.generate_javascript();
        let script_filename = cfg.script_filename();
        let script_zip_path = format!("assets/mtr/panto/{}", script_filename);

        let base_name = if !p.model_display_name.is_empty() {
            p.model_display_name.clone()
        } else {
            Path::new(&p.model_file_name)
                .file_stem()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| "panto".to_string())
        };

        let panto_obj_zip_path = format!("assets/mtr/panto/{}.obj", base_name);
        let mtl_name = format!("{}.mtl", base_name);

        let (tex_zip_path, tex_bytes) = if let Some(ref tex_path) = p.panto_texture_path {
            if Path::new(tex_path).exists() {
                let ext = Path::new(tex_path).extension().and_then(|e| e.to_str()).unwrap_or("png");
                let name = format!("{}.{}", base_name, ext);
                let bytes = std::fs::read(tex_path)
                    .map_err(|e| format!("Failed to read pantograph texture: {}", e))?;
                (Some(format!("assets/mtr/panto/{}", name)), Some(bytes))
            } else {
                (None, None)
            }
        } else {
            (None, None)
        };

        let tex_name = p.panto_texture_path.as_ref().map(|tp| {
            let ext = Path::new(tp).extension().and_then(|e| e.to_str()).unwrap_or("png");
            format!("{}.{}", base_name, ext)
        });

        let mtl_ref = if p.panto_mtl_path.is_some() || tex_name.is_some() {
            Some(mtl_name.as_str())
        } else {
            None
        };
        let panto_obj_bytes = prepare_panto_obj(&p.panto_obj_path, mtl_ref)?;

        let (mtl_zip_path, mtl_bytes) = if let Some(ref mtl_path) = p.panto_mtl_path {
            if Path::new(mtl_path).exists() {
                let bytes = prepare_panto_mtl(mtl_path, tex_name.as_deref(), Some(&panto_obj_bytes))?;
                (Some(format!("assets/mtr/panto/{}", mtl_name)), Some(bytes))
            } else if let Some(ref tname) = tex_name {
                let mut fallback = format!("# Generated by EDPC\nnewmtl panto\nmap_Kd {}\n", tname);
                fallback.push_str("newmtl none\n");
                (Some(format!("assets/mtr/panto/{}", mtl_name)), Some(fallback.into_bytes()))
            } else {
                (None, None)
            }
        } else if let Some(ref tname) = tex_name {
            let mut fallback = format!("# Generated by EDPC\nnewmtl panto\nmap_Kd {}\n", tname);
            fallback.push_str("newmtl none\n");
            (Some(format!("assets/mtr/panto/{}", mtl_name)), Some(fallback.into_bytes()))
        } else {
            (None, None)
        };

        let vehicle_ids: HashSet<String> = assigned.into_iter().map(|v| v.id).collect();

        prepared.push(PreparedPantoAsset {
            script_id: cfg.script_id(),
            script_zip_path,
            script_js_content,
            panto_obj_zip_path,
            panto_obj_bytes,
            mtl_zip_path,
            mtl_bytes,
            tex_zip_path,
            tex_bytes,
            vehicle_ids,
        });
    }

    Ok(prepared)
}

fn package_dir_pack(opts: &PackageOptions) -> Result<String, String> {
    let base_dir = Path::new(&opts.pack_input_path);
    let out_file = File::create(&opts.pack_output_path)
        .map_err(|e| format!("Failed to create output zip: {}", e))?;
    let mut writer = ZipWriter::new(out_file);
    let zip_options = SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated)
        .unix_permissions(0o644);

    let active_pantos = prepare_active_pantographs(opts)?;

    let mut skip_paths: HashSet<String> = HashSet::new();
    for p in &active_pantos {
        skip_paths.insert(p.script_zip_path.clone());
        skip_paths.insert(p.panto_obj_zip_path.clone());
        if let Some(ref mtl) = p.mtl_zip_path {
            skip_paths.insert(mtl.clone());
        }
        if let Some(ref tex) = p.tex_zip_path {
            skip_paths.insert(tex.clone());
        }
    }

    let mut custom_resources_json = String::new();
    let mut written_paths: HashSet<String> = HashSet::new();

    // 1. Walk directory and write files
    for entry in walkdir::WalkDir::new(base_dir).into_iter().filter_map(|e| e.ok()) {
        let entry_path = entry.path();
        if entry_path == base_dir {
            continue;
        }

        let rel_path = entry_path
            .strip_prefix(base_dir)
            .map_err(|e| e.to_string())?
            .to_string_lossy()
            .replace('\\', "/");

        if rel_path == opts.custom_resources_rel_path {
            custom_resources_json = std::fs::read_to_string(entry_path)
                .map_err(|e| format!("Failed to read custom resources JSON: {}", e))?;
            continue;
        }

        if skip_paths.contains(&rel_path) || written_paths.contains(&rel_path) {
            continue;
        }

        if entry.file_type().is_dir() {
            let dir_options = SimpleFileOptions::default().unix_permissions(0o755);
            writer.add_directory(&rel_path, dir_options).map_err(|e| e.to_string())?;
        } else if entry.file_type().is_file() {
            writer.start_file(&rel_path, zip_options).map_err(|e| e.to_string())?;
            let mut f = File::open(entry_path).map_err(|e| e.to_string())?;
            let mut buf = Vec::new();
            f.read_to_end(&mut buf).map_err(|e| e.to_string())?;
            writer.write_all(&buf).map_err(|e| e.to_string())?;
            written_paths.insert(rel_path);
        }
    }

    // 2. Modify and write custom_resources.json
    let active_entries: Vec<ActivePantographEntry> = active_pantos
        .iter()
        .map(|p| {
            let loc = format!("mtr:{}", p.script_zip_path.trim_start_matches("assets/mtr/"));
            ActivePantographEntry {
                script_id: p.script_id.clone(),
                script_location: loc,
                vehicle_ids: p.vehicle_ids.clone(),
            }
        })
        .collect();

    let updated_json = update_custom_resources_json_multi(&custom_resources_json, &active_entries)?;
    writer.start_file(&opts.custom_resources_rel_path, zip_options).map_err(|e| e.to_string())?;
    writer.write_all(updated_json.as_bytes()).map_err(|e| e.to_string())?;

    // 3. Write all pantograph scripts, OBJs, MTLs, and textures
    for p in &active_pantos {
        writer.start_file(&p.script_zip_path, zip_options).map_err(|e| e.to_string())?;
        writer.write_all(p.script_js_content.as_bytes()).map_err(|e| e.to_string())?;

        writer.start_file(&p.panto_obj_zip_path, zip_options).map_err(|e| e.to_string())?;
        writer.write_all(&p.panto_obj_bytes).map_err(|e| e.to_string())?;

        if let (Some(ref mtl_zip), Some(ref mtl_bytes)) = (&p.mtl_zip_path, &p.mtl_bytes) {
            writer.start_file(mtl_zip, zip_options).map_err(|e| e.to_string())?;
            writer.write_all(mtl_bytes).map_err(|e| e.to_string())?;
        }

        if let (Some(ref tex_zip), Some(ref tex_bytes)) = (&p.tex_zip_path, &p.tex_bytes) {
            writer.start_file(tex_zip, zip_options).map_err(|e| e.to_string())?;
            writer.write_all(tex_bytes).map_err(|e| e.to_string())?;
        }
    }

    writer.finish().map_err(|e| format!("Failed to finish zip writer: {}", e))?;
    Ok(opts.pack_output_path.clone())
}

fn package_zip_pack(opts: &PackageOptions) -> Result<String, String> {
    let in_file = File::open(&opts.pack_input_path)
        .map_err(|e| format!("Failed to open input zip: {}", e))?;
    let mut archive = ZipArchive::new(in_file)
        .map_err(|e| format!("Failed to read input zip: {}", e))?;

    let out_file = File::create(&opts.pack_output_path)
        .map_err(|e| format!("Failed to create output zip: {}", e))?;
    let mut writer = ZipWriter::new(out_file);
    let zip_options = SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated)
        .unix_permissions(0o644);

    let active_pantos = prepare_active_pantographs(opts)?;

    let mut skip_paths: HashSet<String> = HashSet::new();
    for p in &active_pantos {
        skip_paths.insert(p.script_zip_path.clone());
        skip_paths.insert(p.panto_obj_zip_path.clone());
        if let Some(ref mtl) = p.mtl_zip_path {
            skip_paths.insert(mtl.clone());
        }
        if let Some(ref tex) = p.tex_zip_path {
            skip_paths.insert(tex.clone());
        }
    }

    let mut custom_resources_json = String::new();
    let mut written_paths: HashSet<String> = HashSet::new();

    // 1. Read existing custom_resources.json and copy all unchanged files
    for i in 0..archive.len() {
        let mut file = archive.by_index(i).map_err(|e| e.to_string())?;
        let name = file.name().to_string();

        if name == opts.custom_resources_rel_path {
            file.read_to_string(&mut custom_resources_json)
                .map_err(|e| format!("Failed to read custom resources JSON: {}", e))?;
            continue; // We will write the updated version later
        }

        if skip_paths.contains(&name) || written_paths.contains(&name) {
            continue;
        }

        if file.is_dir() {
            let dir_options = SimpleFileOptions::default().unix_permissions(0o755);
            writer.add_directory(&name, dir_options).map_err(|e| e.to_string())?;
        } else {
            writer.start_file(&name, zip_options).map_err(|e| e.to_string())?;
            let mut buf = Vec::new();
            file.read_to_end(&mut buf).map_err(|e| e.to_string())?;
            writer.write_all(&buf).map_err(|e| e.to_string())?;
            written_paths.insert(name);
        }
    }

    // 2. Modify custom_resources.json to attach script
    let active_entries: Vec<ActivePantographEntry> = active_pantos
        .iter()
        .map(|p| {
            let loc = format!("mtr:{}", p.script_zip_path.trim_start_matches("assets/mtr/"));
            ActivePantographEntry {
                script_id: p.script_id.clone(),
                script_location: loc,
                vehicle_ids: p.vehicle_ids.clone(),
            }
        })
        .collect();

    let updated_json = update_custom_resources_json_multi(&custom_resources_json, &active_entries)?;

    // 3. Write updated custom_resources.json
    writer.start_file(&opts.custom_resources_rel_path, zip_options).map_err(|e| e.to_string())?;
    writer.write_all(updated_json.as_bytes()).map_err(|e| e.to_string())?;

    // 4. Write all pantograph scripts, OBJs, MTLs, and textures
    for p in &active_pantos {
        writer.start_file(&p.script_zip_path, zip_options).map_err(|e| e.to_string())?;
        writer.write_all(p.script_js_content.as_bytes()).map_err(|e| e.to_string())?;

        writer.start_file(&p.panto_obj_zip_path, zip_options).map_err(|e| e.to_string())?;
        writer.write_all(&p.panto_obj_bytes).map_err(|e| e.to_string())?;

        if let (Some(ref mtl_zip), Some(ref mtl_bytes)) = (&p.mtl_zip_path, &p.mtl_bytes) {
            writer.start_file(mtl_zip, zip_options).map_err(|e| e.to_string())?;
            writer.write_all(mtl_bytes).map_err(|e| e.to_string())?;
        }

        if let (Some(ref tex_zip), Some(ref tex_bytes)) = (&p.tex_zip_path, &p.tex_bytes) {
            writer.start_file(tex_zip, zip_options).map_err(|e| e.to_string())?;
            writer.write_all(tex_bytes).map_err(|e| e.to_string())?;
        }
    }

    writer.finish().map_err(|e| format!("Failed to finish zip writer: {}", e))?;

    Ok(opts.pack_output_path.clone())
}

pub fn update_custom_resources_json_multi(
    original_json: &str,
    active_pantos: &[ActivePantographEntry],
) -> Result<String, String> {
    let mut root: serde_json::Value = serde_json::from_str(original_json)
        .map_err(|e| format!("JSON parse error: {}", e))?;

    let managed_script_ids: HashSet<String> = active_pantos.iter().map(|p| p.script_id.clone()).collect();

    // 1. Process vehicles
    if let Some(vehicles_val) = root.get_mut("vehicles") {
        if let Some(arr) = vehicles_val.as_array_mut() {
            for v in arr {
                let id_opt = v.get("id").and_then(|x| x.as_str());
                let mut matched_panto: Option<&ActivePantographEntry> = None;
                if let Some(id) = id_opt {
                    matched_panto = active_pantos.iter().find(|p| p.vehicle_ids.contains(id));
                }
                let assigned = matched_panto.map(|p| (p.script_id.as_str(), p.script_location.as_str()));
                apply_script_to_vehicle_entry(v, assigned, &managed_script_ids);
            }
        } else if let Some(obj) = vehicles_val.as_object_mut() {
            for (id_str, v) in obj {
                let mut matched_panto = active_pantos.iter().find(|p| p.vehicle_ids.contains(id_str));
                if matched_panto.is_none() {
                    if let Some(inner_id) = v.get("id").and_then(|x| x.as_str()) {
                        matched_panto = active_pantos.iter().find(|p| p.vehicle_ids.contains(inner_id));
                    }
                }
                let assigned = matched_panto.map(|p| (p.script_id.as_str(), p.script_location.as_str()));
                apply_script_to_vehicle_entry(v, assigned, &managed_script_ids);
            }
        }
    }

    // 2. Add or update vehicleScripts array at root
    for p in active_pantos {
        update_vehicle_scripts_entry(&mut root, &p.script_id, &p.script_location);
    }

    serde_json::to_string_pretty(&root).map_err(|e| format!("JSON serialize error: {}", e))
}

#[allow(dead_code)]
fn update_custom_resources_json(
    original_json: &str,
    script_zip_path: &str,
    cfg: &ScriptConfig,
) -> Result<String, String> {
    let panto_ids: HashSet<String> = cfg
        .vehicles
        .iter()
        .filter(|v| v.has_pantograph)
        .map(|v| v.id.clone())
        .collect();

    let script_location = format!("mtr:{}", script_zip_path.trim_start_matches("assets/mtr/"));
    let script_id = cfg.script_id();

    let entry = ActivePantographEntry {
        script_id,
        script_location,
        vehicle_ids: panto_ids,
    };
    update_custom_resources_json_multi(original_json, &[entry])
}

fn apply_script_to_vehicle_entry(
    v: &mut serde_json::Value,
    assigned_panto: Option<(&str, &str)>,
    managed_script_ids: &HashSet<String>,
) {
    if let Some(obj) = v.as_object_mut() {
        if let Some((script_id, script_location)) = assigned_panto {
            // Clean up any pantograph scripts from scriptFiles if present
            if let Some(existing_scripts) = obj.get_mut("scriptFiles") {
                if let Some(arr) = existing_scripts.as_array_mut() {
                    arr.retain(|item| {
                        if let Some(s) = item.as_str() {
                            s != script_location
                                && !s.ends_with("_pantograph.js")
                                && !s.starts_with("mtr:panto/")
                        } else {
                            true
                        }
                    });
                }
            }
            if obj
                .get("scriptFiles")
                .and_then(|v| v.as_array())
                .map_or(false, |a| a.is_empty())
            {
                obj.remove("scriptFiles");
            }

            // Assign scriptId to the vehicle
            obj.insert(
                "scriptId".to_string(),
                serde_json::Value::String(script_id.to_string()),
            );
        } else {
            // If this vehicle previously had one of our managed scriptIds, remove it
            if let Some(existing_id) = obj.get("scriptId").and_then(|x| x.as_str()) {
                if managed_script_ids.contains(existing_id) {
                    obj.remove("scriptId");
                }
            }
        }
    }
}

fn update_vehicle_scripts_entry(
    root: &mut serde_json::Value,
    script_id: &str,
    script_location: &str,
) {
    if let Some(obj) = root.as_object_mut() {
        // Collect all scriptIds currently in use by any vehicle
        let used_script_ids: HashSet<String> = obj
            .get("vehicles")
            .and_then(|v| v.as_array())
            .map(|v_arr| {
                v_arr
                    .iter()
                    .filter_map(|veh| veh.get("scriptId").and_then(|s| s.as_str()).map(|s| s.to_string()))
                    .collect()
            })
            .unwrap_or_default();

        if let Some(vs_val) = obj.get_mut("vehicleScripts") {
            if let Some(arr) = vs_val.as_array_mut() {

                // Clean up any stale pantograph scripts that are no longer used by any vehicle
                arr.retain(|item| {
                    let id = item.get("id").and_then(|x| x.as_str()).unwrap_or("");
                    if id == script_id || used_script_ids.contains(id) {
                        return true;
                    }
                    let is_panto_script = item
                        .get("scriptLocations")
                        .and_then(|l| l.as_array())
                        .map_or(false, |locs| {
                            locs.iter().any(|loc| {
                                loc.as_str().map_or(false, |s| s.starts_with("mtr:panto/"))
                            })
                        })
                        || id.ends_with("_panto");

                    !is_panto_script
                });

                let mut found = false;
                for item in arr.iter_mut() {
                    if item.get("id").and_then(|x| x.as_str()) == Some(script_id) {
                        if let Some(item_obj) = item.as_object_mut() {
                            item_obj.insert("dataFetchMode".to_string(), serde_json::json!("ALL"));
                            item_obj.insert(
                                "scriptLocations".to_string(),
                                serde_json::json!([script_location]),
                            );
                        }
                        found = true;
                        break;
                    }
                }
                if !found {
                    arr.push(serde_json::json!({
                        "id": script_id,
                        "dataFetchMode": "ALL",
                        "scriptLocations": [script_location]
                    }));
                }
                return;
            }
        }

        // vehicleScripts not yet in root: insert at the bottom
        obj.insert(
            "vehicleScripts".to_string(),
            serde_json::json!([
                {
                    "id": script_id,
                    "dataFetchMode": "ALL",
                    "scriptLocations": [script_location]
                }
            ]),
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pack_inspector::VehicleInfo;

    #[test]
    fn test_update_custom_resources_json_panto_id_and_vehicle_scripts() {
        let input_json = r#"{
  "vehicles": [
    {
      "id": "ABB_SGP_1",
      "name": "ABB-SGP (FRONT)"
    },
    {
      "id": "ABB_SGP_TRAILER_1",
      "name": "ABB-SGP TRAILER (FRONT)",
      "scriptFiles": [
        "mtr:panto/scripts/old.js"
      ]
    }
  ],
  "signs": []
}"#;

        let vehicles = vec![
            VehicleInfo {
                id: "ABB_SGP_1".to_string(),
                name: "ABB-SGP (FRONT)".to_string(),
                has_pantograph: false,
                ..Default::default()
            },
            VehicleInfo {
                id: "ABB_SGP_TRAILER_1".to_string(),
                name: "ABB-SGP TRAILER (FRONT)".to_string(),
                has_pantograph: true,
                ..Default::default()
            },
        ];

        let cfg = ScriptConfig {
            random_id: "abb_sgp".to_string(),
            model_file_name: "pantograph.obj".to_string(),
            model_display_name: "Dynamic Pantograph".to_string(),
            wire_tracking_reference: 0.0,
            no_wire_park_height: 0.0,
            enable_depot_park: true,
            depot_park_height: 0.0,
            upper_threshold: 3.0,
            lower_threshold: -3.0,
            vehicles,
        };

        let result = update_custom_resources_json(
            input_json,
            "assets/mtr/panto/abb_sgp_pantograph.js",
            &cfg,
        ).unwrap();

        let val: serde_json::Value = serde_json::from_str(&result).unwrap();

        // Check vehicles
        let veh_arr = val.get("vehicles").unwrap().as_array().unwrap();
        // Vehicle 0 (no panto):
        assert!(veh_arr[0].get("scriptId").is_none());
        assert!(veh_arr[0].get("scriptFiles").is_none());

        // Vehicle 1 (with panto):
        assert_eq!(veh_arr[1].get("scriptId").unwrap().as_str().unwrap(), "abb_sgp_panto");
        assert!(veh_arr[1].get("scriptFiles").is_none());

        // Check vehicleScripts at root
        let vs = val.get("vehicleScripts").unwrap().as_array().unwrap();
        assert_eq!(vs.len(), 1);
        assert_eq!(vs[0].get("id").unwrap().as_str().unwrap(), "abb_sgp_panto");
        assert_eq!(vs[0].get("dataFetchMode").unwrap().as_str().unwrap(), "ALL");
        let locs = vs[0].get("scriptLocations").unwrap().as_array().unwrap();
        assert_eq!(locs[0].as_str().unwrap(), "mtr:panto/abb_sgp_pantograph.js");

        // Verify vehicleScripts is after signs (at bottom)
        let keys: Vec<&String> = val.as_object().unwrap().keys().collect();
        assert_eq!(keys, vec!["vehicles", "signs", "vehicleScripts"]);
    }

    #[test]
    fn test_update_custom_resources_json_preserves_other_scripts() {
        let input_json = r#"{
  "vehicles": [
    {
      "id": "CAR_A",
      "scriptId": "abb_sgp_panto"
    },
    {
      "id": "CAR_B",
      "scriptId": "other_mod_script"
    }
  ],
  "vehicleScripts": [
    {
      "id": "other_mod_script",
      "dataFetchMode": "ALL",
      "scriptLocations": [
        "mtr:scripts/other.js"
      ]
    }
  ]
}"#;

        // In this run, CAR_A is NOT checked for pantograph, but CAR_B has other script
        let vehicles = vec![
            VehicleInfo {
                id: "CAR_A".to_string(),
                has_pantograph: false,
                ..Default::default()
            },
            VehicleInfo {
                id: "CAR_B".to_string(),
                has_pantograph: false,
                ..Default::default()
            },
        ];

        let cfg = ScriptConfig {
            random_id: "abb_sgp".to_string(),
            model_file_name: "pantograph.obj".to_string(),
            model_display_name: "Dynamic Pantograph".to_string(),
            wire_tracking_reference: 0.0,
            no_wire_park_height: 0.0,
            enable_depot_park: true,
            depot_park_height: 0.0,
            upper_threshold: 3.0,
            lower_threshold: -3.0,
            vehicles,
        };

        let result = update_custom_resources_json(
            input_json,
            "assets/mtr/panto/abb_sgp_pantograph.js",
            &cfg,
        ).unwrap();

        let val: serde_json::Value = serde_json::from_str(&result).unwrap();
        let veh_arr = val.get("vehicles").unwrap().as_array().unwrap();

        // CAR_A had abb_sgp_panto previously, now unchecked: removed
        assert!(veh_arr[0].get("scriptId").is_none());

        // CAR_B had other_mod_script: untouched!
        assert_eq!(veh_arr[1].get("scriptId").unwrap().as_str().unwrap(), "other_mod_script");

        // vehicleScripts has both other_mod_script and abb_sgp_panto
        let vs = val.get("vehicleScripts").unwrap().as_array().unwrap();
        assert_eq!(vs.len(), 2);
        assert_eq!(vs[0].get("id").unwrap().as_str().unwrap(), "other_mod_script");
        assert_eq!(vs[1].get("id").unwrap().as_str().unwrap(), "abb_sgp_panto");
    }

    #[test]
    fn test_update_real_abb_sgp_pack_resources() {
        let json_path = "/home/ege/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances/transitra 1.20.1/.minecraft/resourcepacks/ABB-SGP/assets/mtr/mtr_custom_resources.json";
        if !std::path::Path::new(json_path).exists() {
            return;
        }

        let original = std::fs::read_to_string(json_path).unwrap();
        let vehicles = vec![
            VehicleInfo {
                id: "ABB_SGP_TRAILER_1".to_string(),
                has_pantograph: true,
                ..Default::default()
            },
            VehicleInfo {
                id: "ABB_SGP_TRAILER_2".to_string(),
                has_pantograph: true,
                ..Default::default()
            },
            VehicleInfo {
                id: "ABB_SGP_1".to_string(),
                has_pantograph: false,
                ..Default::default()
            },
        ];

        let cfg = ScriptConfig {
            random_id: "abb_sgp".to_string(),
            model_file_name: "pantograph.obj".to_string(),
            model_display_name: "Dynamic Pantograph".to_string(),
            wire_tracking_reference: 0.0,
            no_wire_park_height: 0.0,
            enable_depot_park: true,
            depot_park_height: 0.0,
            upper_threshold: 3.0,
            lower_threshold: -3.0,
            vehicles,
        };

        let result = update_custom_resources_json(
            &original,
            "assets/mtr/panto/abb_sgp_pantograph.js",
            &cfg,
        ).unwrap();

        let val: serde_json::Value = serde_json::from_str(&result).unwrap();
        let vs = val.get("vehicleScripts").unwrap().as_array().unwrap();
        assert_eq!(vs.len(), 1);
        assert_eq!(vs[0].get("id").unwrap().as_str().unwrap(), "abb_sgp_panto");
        assert_eq!(vs[0].get("dataFetchMode").unwrap().as_str().unwrap(), "ALL");
        assert_eq!(
            vs[0].get("scriptLocations").unwrap().as_array().unwrap()[0].as_str().unwrap(),
            "mtr:panto/abb_sgp_pantograph.js"
        );

        let veh_arr = val.get("vehicles").unwrap().as_array().unwrap();
        for v in veh_arr {
            let id = v.get("id").unwrap().as_str().unwrap();
            // scriptFiles should never exist on any vehicle
            assert!(v.get("scriptFiles").is_none(), "Vehicle {} should not have scriptFiles", id);
            if id == "ABB_SGP_TRAILER_1" || id == "ABB_SGP_TRAILER_2" {
                assert_eq!(
                    v.get("scriptId").unwrap().as_str().unwrap(),
                    "abb_sgp_panto",
                    "Vehicle {} should have scriptId abb_sgp_panto",
                    id
                );
            }
        }
    }

    #[test]
    fn test_prepare_panto_obj_and_mtl_material_sync() {
        let temp_dir = std::env::temp_dir().join("edpc_test_packager_sync");
        let _ = std::fs::create_dir_all(&temp_dir);

        let test_obj_path = temp_dir.join("test_model.obj");
        let test_mtl_path = temp_dir.join("test_model.mtl");

        // Write an OBJ with no usemtl and no mtllib
        let obj_text = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\ng p_base\nf 1 2 3\n";
        std::fs::write(&test_obj_path, obj_text).unwrap();

        // Write an MTL with other material
        let mtl_text = "newmtl custom_mat\nmap_Kd old.png\n";
        std::fs::write(&test_mtl_path, mtl_text).unwrap();

        let obj_bytes = prepare_panto_obj(test_obj_path.to_str().unwrap(), Some("model123.mtl")).unwrap();
        let obj_str = String::from_utf8(obj_bytes.clone()).unwrap();

        assert!(obj_str.contains("mtllib model123.mtl"), "Should have updated mtllib");
        assert!(obj_str.contains("usemtl panto"), "Should have injected usemtl panto");

        let mtl_bytes = prepare_panto_mtl(test_mtl_path.to_str().unwrap(), Some("new_tex.png"), Some(&obj_bytes)).unwrap();
        let mtl_str = String::from_utf8(mtl_bytes).unwrap();

        assert!(mtl_str.contains("newmtl panto"), "Should have defined newmtl panto");
        assert!(mtl_str.contains("map_Kd new_tex.png"), "Should have updated map_Kd to new_tex.png");
        assert!(mtl_str.contains("newmtl custom_mat"), "Should have kept custom_mat");
    }
}
