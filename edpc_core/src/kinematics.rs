use crate::obj_parser::{PantographModel, PantographPivots, Vertex};

#[derive(Debug, Clone, Copy, Default)]
pub struct KinematicsAngles {
    pub angle1: f32,
    pub angle2: f32,
    pub angle4: f32,
    pub reachable: bool,
}

#[derive(Debug, Clone)]
pub struct KinematicsSolver {
    pub pivots: PantographPivots,
    pub l1: f32,
    pub l2: f32,
    pub base_angle1: f32,
    pub rest_theta2: f32,
    pub has_p4: bool,
    pub p4_rest_angle: f32,
}

impl KinematicsSolver {
    pub fn new(pivots: &PantographPivots) -> Self {
        let b = pivots.base;
        let e = pivots.elbow;
        let t = pivots.tip;

        let l1 = (e.z - b.z).hypot(e.y - b.y);
        let l2 = (t.z - e.z).hypot(t.y - e.y);

        let base_angle1 = (e.y - b.y).atan2(e.z - b.z);
        let rest_angle2_abs = (t.y - e.y).atan2(t.z - e.z);
        let rest_theta2 = rest_angle2_abs - base_angle1;

        let has_p4 = pivots.p4_base.is_some() && pivots.p4_elbow.is_some();
        let p4_rest_angle = if let (Some(p4_b), Some(p4_e)) = (pivots.p4_base, pivots.p4_elbow) {
            (p4_e.y - p4_b.y).atan2(p4_e.z - p4_b.z)
        } else {
            0.0
        };

        Self {
            pivots: pivots.clone(),
            l1,
            l2,
            base_angle1,
            rest_theta2,
            has_p4,
            p4_rest_angle,
        }
    }

    pub fn solve(&self, current_height: f32) -> KinematicsAngles {
        let base = self.pivots.base;
        let elbow = self.pivots.elbow;
        let tip = self.pivots.tip;

        // Adaptive Z-compensation for low / park heights (identical to dynamic_panto_lib.js)
        let mut target_z = tip.z;
        if current_height < 1.10 {
            let t_fold = ((1.10 - current_height) / (1.10 - 0.40)).clamp(0.0, 1.0);
            let smooth_fold = t_fold * t_fold * (3.0 - 2.0 * t_fold);
            let shift_z = if elbow.z < base.z { -0.035 } else { 0.035 };
            target_z = tip.z + smooth_fold * shift_z;
        }

        let ik_res = self.solve_2link_ik(base.z, base.y, target_z, current_height);
        let ik = match ik_res {
            Some(res) => res,
            None => {
                // Fallback clamp
                let min_reach_h = base.y + (self.l1 - self.l2).abs() + 0.05;
                let safe_h = current_height.max(min_reach_h);
                match self.solve_2link_ik(base.z, base.y, target_z, safe_h) {
                    Some(res) => res,
                    None => return KinematicsAngles::default(),
                }
            }
        };

        let panto_angle1 = -(ik.theta1 - self.base_angle1);
        let panto_angle2 = -(ik.theta2 - self.rest_theta2);
        let mut panto_angle4 = 0.0;

        if self.has_p4 {
            if let (Some(p4_b), Some(p4_e)) = (self.pivots.p4_base, self.pivots.p4_elbow) {
                let dy_cb = p4_e.y - elbow.y;
                let dz_cb = p4_e.z - elbow.z;
                let cy2 = elbow.y + dy_cb * panto_angle2.cos() - dz_cb * panto_angle2.sin();
                let cz2 = elbow.z + dy_cb * panto_angle2.sin() + dz_cb * panto_angle2.cos();

                let dy_ba = cy2 - base.y;
                let dz_ba = cz2 - base.z;
                let cy_world = base.y + dy_ba * panto_angle1.cos() - dz_ba * panto_angle1.sin();
                let cz_world = base.z + dy_ba * panto_angle1.sin() + dz_ba * panto_angle1.cos();

                let current_angle4 = (cy_world - p4_b.y).atan2(cz_world - p4_b.z);
                panto_angle4 = -(current_angle4 - self.p4_rest_angle);
            }
        }

        KinematicsAngles {
            angle1: panto_angle1,
            angle2: panto_angle2,
            angle4: panto_angle4,
            reachable: true,
        }
    }

    fn solve_2link_ik(&self, z_base: f32, y_base: f32, z_target: f32, y_target: f32) -> Option<IkResult> {
        let x = z_target - z_base;
        let y = y_target - y_base;

        let r2 = x * x + y * y;
        let r = r2.sqrt();

        if r > (self.l1 + self.l2) || r < (self.l1 - self.l2).abs() {
            return None;
        }

        let mut cos_theta2 = (r2 - self.l1 * self.l1 - self.l2 * self.l2) / (2.0 * self.l1 * self.l2);
        cos_theta2 = cos_theta2.clamp(-1.0, 1.0);

        let theta2_v1 = cos_theta2.acos();
        let theta2_v2 = -cos_theta2.acos();

        let alpha = y.atan2(x);

        let beta1 = (self.l2 * theta2_v1.sin()).atan2(self.l1 + self.l2 * theta2_v1.cos());
        let theta1_v1 = alpha - beta1;

        let beta2 = (self.l2 * theta2_v2.sin()).atan2(self.l1 + self.l2 * theta2_v2.cos());
        let theta1_v2 = alpha - beta2;

        let v = if theta2_v1.signum() == self.rest_theta2.signum() {
            IkResult { theta1: theta1_v1, theta2: theta2_v1 }
        } else {
            IkResult { theta1: theta1_v2, theta2: theta2_v2 }
        };

        Some(v)
    }

    /// Transforms model vertices for 3D rendering based on angles
    pub fn transform_model(&self, model: &PantographModel, angles: &KinematicsAngles) -> Vec<Vertex> {
        let mut out = Vec::with_capacity(model.vertices.len());
        let base = self.pivots.base;
        let elbow = self.pivots.elbow;
        let tip = self.pivots.tip;
        let p4_base = self.pivots.p4_base.unwrap_or(base);

        let a1 = angles.angle1;
        let a2 = angles.angle2;
        let a4 = angles.angle4;

        for (group_name, verts) in &model.parts {
            match group_name.as_str() {
                "p_base" | "base" => {
                    // Fixed base
                    out.extend_from_slice(verts);
                }
                "p1" => {
                    // Lower arm: rotate around base by a1
                    for v in verts {
                        let mut p = v.pos;
                        let mut n = v.normal;
                        p.y -= base.y; p.z -= base.z;
                        let py = p.y * a1.cos() - p.z * a1.sin();
                        let pz = p.y * a1.sin() + p.z * a1.cos();
                        p.y = py + base.y; p.z = pz + base.z;

                        let ny = n.y * a1.cos() - n.z * a1.sin();
                        let nz = n.y * a1.sin() + n.z * a1.cos();
                        n.y = ny; n.z = nz;

                        out.push(Vertex { pos: p, normal: n, uv: v.uv, part_id: 1 });
                    }
                }
                "p2" => {
                    // Upper arm: in lower arm frame, rotate around elbow by a2
                    for v in verts {
                        let mut p = v.pos;
                        let mut n = v.normal;

                        // Rotate around elbow by a2
                        p.y -= elbow.y; p.z -= elbow.z;
                        let py2 = p.y * a2.cos() - p.z * a2.sin();
                        let pz2 = p.y * a2.sin() + p.z * a2.cos();
                        p.y = py2 + elbow.y; p.z = pz2 + elbow.z;

                        let ny2 = n.y * a2.cos() - n.z * a2.sin();
                        let nz2 = n.y * a2.sin() + n.z * a2.cos();

                        // Rotate around base by a1
                        p.y -= base.y; p.z -= base.z;
                        let py1 = p.y * a1.cos() - p.z * a1.sin();
                        let pz1 = p.y * a1.sin() + p.z * a1.cos();
                        p.y = py1 + base.y; p.z = pz1 + base.z;

                        let ny1 = ny2 * a1.cos() - nz2 * a1.sin();
                        let nz1 = ny2 * a1.sin() + nz2 * a1.cos();
                        n.y = ny1; n.z = nz1;

                        out.push(Vertex { pos: p, normal: n, uv: v.uv, part_id: 2 });
                    }
                }
                "p3" => {
                    // Head: level skid rotated by (-a1 - a2) around tip
                    let a3 = -a1 - a2;
                    for v in verts {
                        let mut p = v.pos;
                        let n = v.normal;

                        // Rotate around tip by a3
                        p.y -= tip.y; p.z -= tip.z;
                        let py3 = p.y * a3.cos() - p.z * a3.sin();
                        let pz3 = p.y * a3.sin() + p.z * a3.cos();
                        p.y = py3 + tip.y; p.z = pz3 + tip.z;

                        // Rotate around elbow by a2
                        p.y -= elbow.y; p.z -= elbow.z;
                        let py2 = p.y * a2.cos() - p.z * a2.sin();
                        let pz2 = p.y * a2.sin() + p.z * a2.cos();
                        p.y = py2 + elbow.y; p.z = pz2 + elbow.z;

                        // Rotate around base by a1
                        p.y -= base.y; p.z -= base.z;
                        let py1 = p.y * a1.cos() - p.z * a1.sin();
                        let pz1 = p.y * a1.sin() + p.z * a1.cos();
                        p.y = py1 + base.y; p.z = pz1 + base.z;

                        out.push(Vertex { pos: p, normal: n, uv: v.uv, part_id: 3 });
                    }
                }
                "p4" => {
                    // Coupler rod: rotate around p4_base by a4
                    for v in verts {
                        let mut p = v.pos;
                        let mut n = v.normal;
                        p.y -= p4_base.y; p.z -= p4_base.z;
                        let py = p.y * a4.cos() - p.z * a4.sin();
                        let pz = p.y * a4.sin() + p.z * a4.cos();
                        p.y = py + p4_base.y; p.z = pz + p4_base.z;

                        let ny = n.y * a4.cos() - n.z * a4.sin();
                        let nz = n.y * a4.sin() + n.z * a4.cos();
                        n.y = ny; n.z = nz;

                        out.push(Vertex { pos: p, normal: n, uv: v.uv, part_id: 4 });
                    }
                }
                _ => {
                    out.extend_from_slice(verts);
                }
            }
        }

        out
    }
}

#[derive(Debug, Clone, Copy)]
struct IkResult {
    theta1: f32,
    theta2: f32,
}
