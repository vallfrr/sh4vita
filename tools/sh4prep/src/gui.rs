//! Compact, localized desktop interface. Copying stays on a worker thread.
use crate::{i18n, prep};
use eframe::egui::{self, Color32, RichText};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

const BACKGROUND: Color32 = Color32::from_rgb(14, 14, 14);
const CARD: Color32 = Color32::from_rgb(26, 26, 26);
const ACCENT: Color32 = Color32::from_rgb(238, 238, 238);
const MUTED: Color32 = Color32::from_rgb(140, 140, 140);
const LINE: Color32 = Color32::from_rgb(58, 58, 58);

#[derive(Default)]
enum Phase {
    #[default]
    Idle,
    Checking,
    Copying,
    Complete(PathBuf),
    Failed(prep::Error),
}

#[derive(Default)]
struct Shared {
    phase: Phase,
    done: u64,
    total: u64,
    files: usize,
    current: String,
}

impl Shared {
    fn busy(&self) -> bool { matches!(self.phase, Phase::Checking | Phase::Copying) }
}

struct App {
    game: String,
    out: String,
    lang: usize,
    st: Arc<Mutex<Shared>>,
}

pub fn run() {
    let opts = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([620.0, 515.0])
            .with_min_inner_size([540.0, 470.0]),
        ..Default::default()
    };
    let app = App { game: String::new(), out: String::new(), lang: 1, st: Arc::default() };
    let result = eframe::run_native("sh4prep · Silent Hill 4 · PS Vita", opts, Box::new(|cc| {
        cc.egui_ctx.set_theme(egui::Theme::Dark);
        let mut style = (*cc.egui_ctx.style_of(egui::Theme::Dark)).clone();
        style.visuals = egui::Visuals::dark();
        style.visuals.panel_fill = BACKGROUND;
        style.visuals.extreme_bg_color = Color32::from_rgb(8, 8, 8);
        style.visuals.override_text_color = Some(Color32::from_rgb(232, 232, 232));
        style.visuals.selection.bg_fill = Color32::from_rgb(85, 85, 85);
        style.visuals.widgets.inactive.bg_fill = CARD;
        style.visuals.widgets.inactive.weak_bg_fill = CARD;
        style.visuals.widgets.inactive.corner_radius = 4.into();
        style.visuals.widgets.hovered.bg_fill = Color32::from_rgb(44, 44, 44);
        style.visuals.widgets.hovered.weak_bg_fill = Color32::from_rgb(44, 44, 44);
        style.visuals.widgets.inactive.bg_stroke = egui::Stroke::new(1.0, LINE);
        style.visuals.widgets.hovered.bg_stroke = egui::Stroke::new(1.0, MUTED);
        style.spacing.item_spacing = egui::vec2(8.0, 6.0);
        style.spacing.button_padding = egui::vec2(12.0, 7.0);
        style.text_styles.insert(egui::TextStyle::Body, egui::FontId::proportional(14.0));
        style.text_styles.insert(egui::TextStyle::Button, egui::FontId::proportional(14.0));
        cc.egui_ctx.set_style_of(egui::Theme::Dark, style);
        Ok(Box::new(app))
    }));
    if let Err(error) = result { eprintln!("sh4prep: {error}"); }
}

fn folder(ui: &mut egui::Ui, id: &str, label: &str, hint: &str, browse: &str, value: &mut String) {
    ui.label(RichText::new(label).strong());
    ui.horizontal(|ui| {
        let width = (ui.available_width() - 124.0).max(140.0);
        ui.add_sized([width, 30.0], egui::TextEdit::singleline(value).id_salt(id).margin(egui::vec2(8.0, 6.0)));
        if ui.add_sized([116.0, 30.0], egui::Button::new(browse)
            .fill(BACKGROUND).stroke(egui::Stroke::new(1.0, LINE))).clicked() {
            if let Some(path) = rfd::FileDialog::new().set_title(label).pick_folder() {
                *value = path.display().to_string();
            }
        }
    });
    ui.label(RichText::new(hint).size(12.0).color(MUTED));
}

impl App {
    fn start(&self, ctx: egui::Context) {
        let (game, out, lang) = (PathBuf::from(&self.game), PathBuf::from(&self.out), prep::LANGUAGES[self.lang]);
        let st = self.st.clone();
        *st.lock().unwrap() = Shared { phase: Phase::Checking, ..Default::default() };
        std::thread::spawn(move || {
            let res = prep::check(&game).and_then(|plan| {
                {
                    let mut s = st.lock().unwrap();
                    s.phase = Phase::Copying;
                    s.total = plan.bytes;
                    s.files = plan.files.len();
                }
                prep::prepare(&plan, &out, lang, |done, file| {
                    let mut s = st.lock().unwrap();
                    s.done = done;
                    s.current = file.display().to_string();
                })
            });
            st.lock().unwrap().phase = match res {
                Ok(dir) => { open_folder(dir.parent().unwrap_or(&dir)); Phase::Complete(dir) }
                Err(error) => Phase::Failed(error),
            };
            ctx.request_repaint();
        });
    }
}

impl eframe::App for App {
    fn ui(&mut self, root: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let busy = self.st.lock().unwrap().busy();
        let ctx = root.ctx().clone();
        let text = &i18n::TEXT[self.lang];
        egui::CentralPanel::default().frame(egui::Frame::new().fill(BACKGROUND).inner_margin(22)).show(root, |ui| {
            egui::ScrollArea::vertical().show(ui, |ui| {
                ui.label(RichText::new("SH4PREP  /  PS VITA").size(11.0).color(ACCENT).strong());
                ui.label(RichText::new(text.title).size(25.0).strong());
                ui.label(RichText::new(text.intro).color(MUTED));
                ui.add_space(10.0);
                // Language is the first editable control, and owns both UI and JSON language.
                ui.add_enabled_ui(!busy, |ui| {
                    ui.horizontal(|ui| {
                        ui.label(RichText::new(text.language).strong());
                        egui::ComboBox::from_id_salt("language").width(150.0)
                            .selected_text(i18n::NAMES[self.lang]).show_ui(ui, |ui| {
                                for (i, name) in i18n::NAMES.iter().enumerate() {
                                    ui.selectable_value(&mut self.lang, i, *name);
                                }
                            });
                    });
                });
                let text = &i18n::TEXT[self.lang];
                ui.label(RichText::new(text.language_hint).size(12.0).color(MUTED));
                ui.add_space(10.0);
                egui::Frame::new().fill(CARD).stroke(egui::Stroke::new(1.0, LINE)).corner_radius(6).inner_margin(14).show(ui, |ui| {
                    ui.add_enabled_ui(!busy, |ui| {
                        folder(ui, "game", text.game, text.game_hint, text.browse, &mut self.game);
                        ui.add_space(9.0);
                        folder(ui, "output", text.output, text.output_hint, text.browse, &mut self.out);
                    });
                });
                ui.add_space(8.0);
                let ready = !busy && !self.game.trim().is_empty() && !self.out.trim().is_empty();
                let button = egui::Button::new(RichText::new(text.prepare).color(BACKGROUND).strong())
                    .fill(ACCENT).min_size(egui::vec2(ui.available_width(), 37.0));
                if ui.add_enabled(ready, button).clicked() { self.start(ctx.clone()); }
                ui.add_space(8.0);

                let s = self.st.lock().unwrap();
                match &s.phase {
                    Phase::Idle => { ui.label(RichText::new(text.ready).color(MUTED)); }
                    Phase::Checking => { ui.horizontal(|ui| { ui.spinner(); ui.label(text.checking); }); }
                    Phase::Copying => {
                        ui.horizontal(|ui| { ui.spinner(); ui.label(text.copying); });
                        ui.add(egui::ProgressBar::new(s.done as f32 / s.total.max(1) as f32).fill(ACCENT).show_percentage());
                        ui.label(RichText::new(format!("{:.2} / {:.2} GB · {} {}", s.done as f64 / 1e9, s.total as f64 / 1e9, s.files, text.files)).size(12.0).color(MUTED));
                        ui.add(egui::Label::new(RichText::new(&s.current).size(12.0).color(MUTED)).truncate()).on_hover_text(&s.current);
                    }
                    Phase::Complete(dir) => {
                        ui.label(RichText::new(text.complete).strong().color(ACCENT));
                        ui.label(text.transfer);
                        ui.add(egui::Label::new(dir.display().to_string()).wrap());
                    }
                    Phase::Failed(error) => {
                        ui.label(RichText::new(text.failed).strong().color(ACCENT));
                        ui.add(egui::Label::new(text.error(error)).wrap());
                    }
                }
                ui.add_space(10.0);
                ui.separator();
                ui.label(RichText::new(text.footer).size(11.0).color(MUTED));
            });
        });
        if busy { ctx.request_repaint_after(std::time::Duration::from_millis(100)); }
    }
}

/// Shows the destination folder (the one that contains `sh4`) in the system file manager.
fn open_folder(path: &std::path::Path) {
    let opener = if cfg!(windows) { "explorer" } else if cfg!(target_os = "macos") { "open" } else { "xdg-open" };
    let _ = std::process::Command::new(opener).arg(path).spawn();
}
