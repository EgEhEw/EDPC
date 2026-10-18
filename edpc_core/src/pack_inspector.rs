use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::Read;
use std::path::Path;
use zip::ZipArchive;

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct VehicleInfo {
    pub id: String,
    pub name: String,
    pub description: String,
    pub has_pantograph: bool,
    pub mount_height: f32,
    pub mount_x: f32,
    pub mount_z: f32,
    pub rotation_y_deg: f32,
    pub wire_detect_z: f32,
    #[serde(default)]
    pub pantograph_id: Option<String>,
    #[serde(default)]
    pub model_resource: Option<String>,
    #[serde(default)]
    pub texture_resource: Option<String>,
    #[serde(default)]
    pub model_properties_resource: Option<String>,
    #[serde(default)]
    pub position_definitions_resource: Option<String>,
    #[serde(default)]
    pub bogie1_model_resource: Option<String>,
    #[serde(default)]
    pub bogie1_properties_resource: Option<String>,
    #[serde(default)]
    pub bogie1_definitions_resource: Option<String>,
    #[serde(default)]
    pub bogie1_position: f32,
    #[serde(default)]
    pub bogie2_model_resource: Option<String>,
    #[serde(default)]
    pub bogie2_properties_resource: Option<String>,
    #[serde(default)]
    pub bogie2_definitions_resource: Option<String>,
    #[serde(default)]
    pub bogie2_position: f32,
    #[serde(default)]
    pub has_gangway1: bool,
    #[serde(default)]
    pub has_gangway2: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackInspectionResult {
    pub vehicles: Vec<VehicleInfo>,
    pub custom_resources_path: String,
    pub is_zip: bool,
}

pub fn inspect_pack(path_str: &str) -> Result<PackInspectionResult, String> {
    let path = Path::new(path_str);
    if !path.exists() {
        return Err(format!("File or folder does not exist: {}", path_str));
    }

    if path.is_file() && path.extension().map_or(false, |ext| ext == "zip") {
        inspect_zip(path)
    } else if path.is_dir() {
        inspect_dir(path)
    } else {
        Err("Unsupported pack format. Please select a .zip file or an extracted folder.".to_string())
    }
}

fn inspect_zip(path: &Path) -> Result<PackInspectionResult, String> {
    let file = File::open(path).map_err(|e| format!("Failed to open zip: {}", e))?;
    let mut archive = ZipArchive::new(file).map_err(|e| format!("Failed to read zip: {}", e))?;

    // Candidate resource files
    let candidates = [
        "assets/mtr/mtr_custom_resources.json",
        "assets/mtr/custom_resources.json",
        "mtr_custom_resources.json",
        "custom_resources.json",
    ];

    let mut found_path = None;
    let mut json_content = String::new();

    for candidate in &candidates {
        if let Ok(mut zf) = archive.by_name(candidate) {
            zf.read_to_string(&mut json_content)
                .map_err(|e| format!("Failed to read {}: {}", candidate, e))?;
            found_path = Some(candidate.to_string());
            break;
        }
    }

    // Fallback: search case-insensitively
    if found_path.is_none() {
        for i in 0..archive.len() {
            if let Ok(mut zf) = archive.by_index(i) {
                let name = zf.name().to_lowercase();
                if name.ends_with("custom_resources.json") {
                    let actual_name = zf.name().to_string();
                    zf.read_to_string(&mut json_content)
                        .map_err(|e| format!("Failed to read {}: {}", actual_name, e))?;
                    found_path = Some(actual_name);
                    break;
                }
            }
        }
    }

    let custom_resources_path = found_path
        .ok_or_else(|| "Could not find 'mtr_custom_resources.json' or 'custom_resources.json' inside the zip.".to_string())?;

    let vehicles = parse_vehicles_from_json(&json_content)?;

    Ok(PackInspectionResult {
        vehicles,
        custom_resources_path,
        is_zip: true,
    })
}

fn inspect_dir(path: &Path) -> Result<PackInspectionResult, String> {
    let candidates = [
        path.join("assets/mtr/mtr_custom_resources.json"),
        path.join("assets/mtr/custom_resources.json"),
        path.join("mtr_custom_resources.json"),
        path.join("custom_resources.json"),
    ];

    let mut found_path = None;
    let mut json_content = String::new();

    for candidate in &candidates {
        if candidate.exists() {
            json_content = std::fs::read_to_string(candidate)
                .map_err(|e| format!("Failed to read {}: {}", candidate.display(), e))?;
            let rel = candidate.strip_prefix(path).unwrap_or(candidate);
            found_path = Some(rel.to_string_lossy().to_string());
            break;
        }
    }

    let custom_resources_path = found_path
        .ok_or_else(|| "Could not find 'mtr_custom_resources.json' in folder.".to_string())?;

    let vehicles = parse_vehicles_from_json(&json_content)?;

    Ok(PackInspectionResult {
        vehicles,
        custom_resources_path,
        is_zip: false,
    })
}

fn parse_vehicles_from_json(json_str: &str) -> Result<Vec<VehicleInfo>, String> {
    let root: serde_json::Value = serde_json::from_str(json_str)
        .map_err(|e| format!("JSON parse error in custom resources: {}", e))?;

    let mut out = Vec::new();

    // Check "vehicles" array or map
    if let Some(vehicles_val) = root.get("vehicles") {
        if let Some(arr) = vehicles_val.as_array() {
            for v in arr {
                if let Some(info) = parse_single_vehicle(v) {
                    out.push(info);
                }
            }
        } else if let Some(obj) = vehicles_val.as_object() {
            for (key, v) in obj {
                let mut info = parse_single_vehicle(v).unwrap_or_else(|| VehicleInfo {
                    id: key.clone(),
                    name: key.clone(),
                    description: String::new(),
                    has_pantograph: false,
                    mount_height: 2.568,
                    mount_x: 0.0,
                    mount_z: 1.875,
                    rotation_y_deg: 0.0,
                    wire_detect_z: 1.92,
                    pantograph_id: None,
                    model_resource: None,
                    texture_resource: None,
                    model_properties_resource: None,
                    position_definitions_resource: None,
                    bogie1_model_resource: None,
                    bogie1_properties_resource: None,
                    bogie1_definitions_resource: None,
                    bogie1_position: 0.0,
                    bogie2_model_resource: None,
                    bogie2_properties_resource: None,
                    bogie2_definitions_resource: None,
                    bogie2_position: 0.0,
                    has_gangway1: false,
                    has_gangway2: false,
                });
                if info.id.is_empty() {
                    info.id = key.clone();
                }
                out.push(info);
            }
        }
    }

    Ok(out)
}

fn parse_single_vehicle(v: &serde_json::Value) -> Option<VehicleInfo> {
    let id = v.get("id").and_then(|x| x.as_str()).unwrap_or("").to_string();
    if id.is_empty() {
        return None;
    }

    let name = v.get("name").and_then(|x| x.as_str()).unwrap_or(&id).to_string();
    let desc = v.get("description").and_then(|x| x.as_str()).unwrap_or("").to_string();

    // Check if ID or vehicle attributes indicate a pantograph candidate
    let lower_id = id.to_lowercase();
    let has_script = v.get("scriptId")
        .and_then(|x| x.as_str())
        .map(|s| !s.trim().is_empty())
        .unwrap_or(false);

    let is_trailer = has_script || (
        (lower_id.contains("trailer") || lower_id.contains("panto"))
        && !lower_id.contains("np")
        && !lower_id.contains("non")
    );
    let is_backward = lower_id.contains("back")
        || lower_id.contains("_b")
        || lower_id.ends_with("b")
        || lower_id.contains("-b")
        || lower_id.contains("2");
    let rotation = if is_backward { 0.0 } else { 180.0 };
    let z_mount = if is_backward { -1.875 } else { 1.875 };
    let wire_detect = if is_backward { -1.92 } else { 1.92 };

    let mut model_resource = None;
    let mut texture_resource = None;
    let mut model_properties_resource = None;
    let mut position_definitions_resource = None;
    if let Some(models_arr) = v.get("models").and_then(|m| m.as_array()) {
        if let Some(m0) = models_arr.first() {
            model_resource = m0.get("modelResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            texture_resource = m0.get("textureResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            model_properties_resource = m0.get("modelPropertiesResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            position_definitions_resource = m0.get("positionDefinitionsResource").and_then(|x| x.as_str()).map(|s| s.to_string());
        }
    }

    let bogie1_position = v.get("bogie1Position").and_then(|x| x.as_f64()).unwrap_or(0.0) as f32;
    let bogie2_position = v.get("bogie2Position").and_then(|x| x.as_f64()).unwrap_or(0.0) as f32;

    let mut bogie1_model_resource = None;
    let mut bogie1_properties_resource = None;
    let mut bogie1_definitions_resource = None;
    if let Some(b1_arr) = v.get("bogie1Models").and_then(|m| m.as_array()) {
        if let Some(b0) = b1_arr.first() {
            bogie1_model_resource = b0.get("modelResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            bogie1_properties_resource = b0.get("modelPropertiesResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            bogie1_definitions_resource = b0.get("positionDefinitionsResource").and_then(|x| x.as_str()).map(|s| s.to_string());
        }
    }

    let mut bogie2_model_resource = None;
    let mut bogie2_properties_resource = None;
    let mut bogie2_definitions_resource = None;
    if let Some(b2_arr) = v.get("bogie2Models").and_then(|m| m.as_array()) {
        if let Some(b0) = b2_arr.first() {
            bogie2_model_resource = b0.get("modelResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            bogie2_properties_resource = b0.get("modelPropertiesResource").and_then(|x| x.as_str()).map(|s| s.to_string());
            bogie2_definitions_resource = b0.get("positionDefinitionsResource").and_then(|x| x.as_str()).map(|s| s.to_string());
        }
    }

    let has_gangway1 = v.get("hasGangway1").and_then(|x| x.as_bool()).unwrap_or(false);
    let has_gangway2 = v.get("hasGangway2").and_then(|x| x.as_bool()).unwrap_or(false);

    Some(VehicleInfo {
        id,
        name,
        description: desc,
        has_pantograph: is_trailer,
        mount_height: 2.568,
        mount_x: 0.0,
        mount_z: z_mount,
        rotation_y_deg: rotation,
        wire_detect_z: wire_detect,
        pantograph_id: None,
        model_resource,
        texture_resource,
        model_properties_resource,
        position_definitions_resource,
        bogie1_model_resource,
        bogie1_properties_resource,
        bogie1_definitions_resource,
        bogie1_position,
        bogie2_model_resource,
        bogie2_properties_resource,
        bogie2_definitions_resource,
        bogie2_position,
        has_gangway1,
        has_gangway2,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_inspect_abb_sgp_pack() {
        let pack_path = "/home/ege/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances/transitra 1.20.1/.minecraft/resourcepacks/ABB-SGP";
        if !std::path::Path::new(pack_path).exists() {
            return;
        }

        let res = inspect_pack(pack_path).expect("Failed to inspect ABB-SGP pack");
        assert_eq!(res.vehicles.len(), 5);

        let t1 = res.vehicles.iter().find(|v| v.id == "ABB_SGP_TRAILER_1").expect("TRAILER_1 missing");
        assert!(t1.has_pantograph);
        assert_eq!(t1.mount_height, 2.568);
        assert_eq!(t1.mount_x, 0.0);
        assert_eq!(t1.mount_z, 1.875);
        assert_eq!(t1.rotation_y_deg, 180.0);
        assert_eq!(t1.wire_detect_z, 1.92);

        let t2 = res.vehicles.iter().find(|v| v.id == "ABB_SGP_TRAILER_2").expect("TRAILER_2 missing");
        assert!(t2.has_pantograph);
        assert_eq!(t2.mount_height, 2.568);
        assert_eq!(t2.mount_x, 0.0);
        assert_eq!(t2.mount_z, -1.875);
        assert_eq!(t2.rotation_y_deg, 0.0);
        assert_eq!(t2.wire_detect_z, -1.92);

        let t_np = res.vehicles.iter().find(|v| v.id == "ABB_SGP_TRAILER_1np").expect("TRAILER_1np missing");
        assert!(!t_np.has_pantograph);

        let cab = res.vehicles.iter().find(|v| v.id == "ABB_SGP").expect("ABB_SGP cab missing");
        assert!(!cab.has_pantograph);
    }

    #[test]
    fn test_load_trailer_mesh() {
        let pack_path = "/home/ege/.var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher/instances/transitra 1.20.1/.minecraft/resourcepacks/ABB-SGP";
        if !std::path::Path::new(pack_path).exists() {
            return;
        }

        let res = crate::vehicle_parser::load_vehicle_mesh_modular(
            pack_path,
            "mtr:abb.bbmodel",
            Some("mtr:vehicles/trailerfront/properties_eabe440d.json"),
            Some("mtr:vehicles/trailerfront/definition_60366303.json"),
            None, None, None, 0.0,
            None, None, None, 0.0,
        ).expect("Failed to load trailer mesh");

        let min_z = res.iter().map(|v| v.pos.z).fold(f32::INFINITY, f32::min);
        let max_z = res.iter().map(|v| v.pos.z).fold(f32::NEG_INFINITY, f32::max);
        println!("Full mesh Z bounds: [{:.3}, {:.3}]", min_z, max_z);

        let ac_verts: Vec<_> = res.iter().filter(|v| v.pos.y > 2.6).collect();
        let ac_min_z = ac_verts.iter().map(|v| v.pos.z).fold(f32::INFINITY, f32::min);
        let ac_max_z = ac_verts.iter().map(|v| v.pos.z).fold(f32::NEG_INFINITY, f32::max);
        println!("AC unit verts count: {}, Z bounds: [{:.3}, {:.3}]", ac_verts.len(), ac_min_z, ac_max_z);
    }
}
