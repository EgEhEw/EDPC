use std::collections::HashMap;

#[derive(Debug, Clone, Copy, Default)]
#[repr(C)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub fn dist(&self, other: &Vec3) -> f32 {
        let dx = self.x - other.x;
        let dy = self.y - other.y;
        let dz = self.z - other.z;
        (dx * dx + dy * dy + dz * dz).sqrt()
    }
}

#[derive(Debug, Clone, Copy, Default)]
#[repr(C)]
pub struct Vec2 {
    pub u: f32,
    pub v: f32,
}

impl Vec2 {
    pub fn new(u: f32, v: f32) -> Self {
        Self { u, v }
    }
}

#[derive(Debug, Clone, Copy, Default)]
#[repr(C)]
pub struct Vertex {
    pub pos: Vec3,
    pub normal: Vec3,
    pub uv: Vec2,
    pub part_id: u32, // 0: base, 1: p1, 2: p2, 3: p3, 4: p4, 5: other
}

#[derive(Debug, Clone, Default)]
pub struct PantographPivots {
    pub base: Vec3,
    pub elbow: Vec3,
    pub tip: Vec3,
    pub shoe_offset: f32,
    pub p4_base: Option<Vec3>,
    pub p4_elbow: Option<Vec3>,
}

#[derive(Debug, Clone, Default)]
pub struct PantographModel {
    pub vertices: Vec<Vertex>,
    pub parts: HashMap<String, Vec<Vertex>>,
    pub pivots: PantographPivots,
    pub orig_pivots: PantographPivots,
    pub raw_obj_text: String,
    pub mtl_filename: Option<String>,
}

impl PantographModel {
    pub fn normalize_group_name(raw: &str) -> String {
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

    pub fn from_obj_string(obj_text: &str) -> Result<Self, String> {
        let mut explicit_pivots: HashMap<String, Vec3> = HashMap::new();
        let mut group_verts: HashMap<String, Vec<Vec3>> = HashMap::new();
        let mut raw_positions: Vec<Vec3> = Vec::new();
        let mut raw_normals: Vec<Vec3> = Vec::new();
        let mut raw_uvs: Vec<Vec2> = Vec::new();
        let mut cur_group = "p_base".to_string();
        let mut mtl_filename: Option<String> = None;

        let mut part_vertices: HashMap<String, Vec<Vertex>> = HashMap::new();

        for line in obj_text.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }

            // Pivot headers: # pivot <part> <x> <y> <z>
            if line.starts_with('#') {
                let parts: Vec<&str> = line.split_whitespace().collect();
                if parts.len() >= 6 && parts[1].eq_ignore_ascii_case("pivot") {
                    let part = parts[2].to_lowercase();
                    if let (Ok(x), Ok(y), Ok(z)) = (
                        parts[3].parse::<f32>(),
                        parts[4].parse::<f32>(),
                        parts[5].parse::<f32>(),
                    ) {
                        explicit_pivots.insert(part, Vec3::new(x, y, z));
                    }
                }
                continue;
            }

            if line.starts_with("mtllib ") {
                let mtl = line[7..].trim();
                mtl_filename = Some(mtl.to_string());
                continue;
            }

            if line.starts_with("g ") {
                let g = line[2..].trim().to_lowercase();
                if !g.is_empty() {
                    cur_group = Self::normalize_group_name(&g);
                }
                continue;
            }

            if line.starts_with("o ") {
                let o = line[2..].trim().to_lowercase();
                let normalized = Self::normalize_group_name(&o);
                let is_panto_part = normalized == "p_base" || normalized == "p1" || normalized == "p2" || normalized == "p3" || normalized == "p4";
                let cur_is_panto_part = cur_group == "p_base" || cur_group == "p1" || cur_group == "p2" || cur_group == "p3" || cur_group == "p4";

                if is_panto_part || !cur_is_panto_part {
                    cur_group = normalized;
                }
                continue;
            }

            if line.starts_with("vt ") {
                let parts: Vec<&str> = line[3..].split_whitespace().collect();
                if parts.len() >= 2 {
                    if let (Ok(u), Ok(v)) = (
                        parts[0].parse::<f32>(),
                        parts[1].parse::<f32>(),
                    ) {
                        // In OBJ, V=0 is bottom, V=1 is top. In Vulkan, V=0 is top.
                        // Flip V: 1.0 - v
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
                        let pos = Vec3::new(x, y, z);
                        raw_positions.push(pos);
                        group_verts.entry(cur_group.clone()).or_default().push(pos);
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
                        raw_positions.get((raw_positions.len() as isize + v_idx) as usize).copied()
                    } else {
                        None
                    };

                    let uv = if sub_tokens.len() >= 2 && !sub_tokens[1].is_empty() {
                        let vt_idx = sub_tokens[1].parse::<isize>().unwrap_or(0);
                        if vt_idx > 0 {
                            raw_uvs.get((vt_idx - 1) as usize).copied()
                        } else if vt_idx < 0 {
                            raw_uvs.get((raw_uvs.len() as isize + vt_idx) as usize).copied()
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
                            raw_normals.get((raw_normals.len() as isize + n_idx) as usize).copied()
                        } else {
                            None
                        }
                    } else {
                        None
                    };

                    if let Some(p) = pos {
                        let n = norm.unwrap_or(Vec3::new(0.0, 1.0, 0.0));
                        let part_id = match cur_group.as_str() {
                            "p_base" => 0,
                            "p1" => 1,
                            "p2" => 2,
                            "p3" => 3,
                            "p4" => 4,
                            _ => 5,
                        };
                        face_verts.push(Vertex {
                            pos: p,
                            normal: n,
                            uv: uv.unwrap_or_default(),
                            part_id,
                        });
                    }
                }

                // Triangulate face fan
                if face_verts.len() >= 3 {
                    let group_list = part_vertices.entry(cur_group.clone()).or_default();
                    for i in 1..(face_verts.len() - 1) {
                        group_list.push(face_verts[0]);
                        group_list.push(face_verts[i]);
                        group_list.push(face_verts[i + 1]);
                    }
                }
            }
        }

        // Resolve pivots (from explicit headers or clustering)
        let mut pivots = Self::resolve_pivots(&explicit_pivots, &group_verts);

        // Compute contact shoe offset relative to tip pivot (distance from tip pivot to flat contact surface on p3)
        let shoe_top = part_vertices.get("p3").and_then(|verts| {
            verts.iter().map(|v| v.pos.y).max_by(|a, b| a.partial_cmp(b).unwrap())
        }).unwrap_or(pivots.tip.y + 0.10484);
        pivots.shoe_offset = (shoe_top - pivots.tip.y).max(0.0);

        let mut all_vertices = Vec::new();
        for (_, verts) in &part_vertices {
            all_vertices.extend_from_slice(verts);
        }

        let orig_pivots = pivots.clone();
        Ok(Self {
            vertices: all_vertices,
            parts: part_vertices,
            pivots,
            orig_pivots,
            raw_obj_text: obj_text.to_string(),
            mtl_filename,
        })
    }

    fn resolve_pivots(
        explicit: &HashMap<String, Vec3>,
        groups: &HashMap<String, Vec<Vec3>>,
    ) -> PantographPivots {
        let base = explicit.get("p1").copied().or_else(|| {
            // Find base as center of lowest vertices of p1 or p_base
            groups.get("p1").and_then(|v| {
                let mut v_sorted = v.clone();
                v_sorted.sort_by(|a, b| a.y.partial_cmp(&b.y).unwrap());
                let take = v_sorted.len().min(8);
                if take == 0 {
                    return None;
                }
                let avg_y = v_sorted[..take].iter().map(|p| p.y).sum::<f32>() / take as f32;
                let avg_z = v_sorted[..take].iter().map(|p| p.z).sum::<f32>() / take as f32;
                Some(Vec3::new(0.0, avg_y, avg_z))
            })
        }).unwrap_or(Vec3::new(0.0, 0.18357, 0.11706));

        let elbow = explicit.get("p2").copied().or_else(|| {
            // Find closest cluster between p1 and p2
            if let (Some(v1), Some(v2)) = (groups.get("p1"), groups.get("p2")) {
                Self::find_closest_cluster(v1, v2, 0.20)
            } else {
                None
            }
        }).unwrap_or(Vec3::new(0.0, 0.83591, -1.21875));

        let tip = explicit.get("p3").copied().or_else(|| {
            // Find closest cluster between p2 and p3
            if let (Some(v2), Some(v3)) = (groups.get("p2"), groups.get("p3")) {
                Self::find_closest_cluster(v2, v3, 0.20)
            } else {
                None
            }
        }).unwrap_or(Vec3::new(0.0, 1.42328, 0.0875));

        let p4_base = explicit.get("p4").copied();
        let p4_elbow = explicit.get("p4_elbow").copied();

        PantographPivots {
            base,
            elbow,
            tip,
            shoe_offset: 0.0,
            p4_base,
            p4_elbow,
        }
    }

    fn find_closest_cluster(v1: &[Vec3], v2: &[Vec3], max_d: f32) -> Option<Vec3> {
        let mut min_dist = f32::MAX;
        let mut best_pair = None;

        for a in v1 {
            for b in v2 {
                let d = a.dist(b);
                if d < min_dist && d <= max_d {
                    min_dist = d;
                    best_pair = Some((*a, *b));
                }
            }
        }

        best_pair.map(|(a, b)| Vec3::new(0.0, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5))
    }
}
