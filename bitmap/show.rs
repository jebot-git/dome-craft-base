// SPDX-License-Identifier: MIT
// Downstream native player. Uses the upstream show order and animation timeline.
pub struct DomeShow {
    state: deckcraft_anim::ShowState,
    step_clock: std::time::Instant,
    slide_clock: std::time::Instant,
    transition_from: Option<usize>,
}
impl DomeShow {
    pub fn new(pres: &deckcraft_model::Presentation, current: usize) -> Self {
        Self {
            state: deckcraft_anim::ShowState::start_at(pres, current),
            step_clock: std::time::Instant::now(),
            slide_clock: std::time::Instant::now(),
            transition_from: None,
        }
    }
    pub fn frame(
        &mut self,
        pres: &deckcraft_model::Presentation,
        action: &str,
    ) -> std::result::Result<serde_json::Value, String> {
        let old = self.state.slide;
        let nav = match action {
            "next" => self.state.next(pres),
            "previous" => self.state.prev(pres),
            _ => deckcraft_anim::ShowAction::None,
        };
        if matches!(nav, deckcraft_anim::ShowAction::Exit) {
            return Ok(serde_json::json!({"exit":true}));
        }
        if self.state.slide != old {
            self.transition_from = Some(old);
            self.slide_clock = std::time::Instant::now();
        }
        if matches!(
            nav,
            deckcraft_anim::ShowAction::Slide(_) | deckcraft_anim::ShowAction::PlayStep(_)
        ) {
            self.step_clock = std::time::Instant::now();
        }
        let timeline = deckcraft_anim::ShowState::timeline(pres, self.state.slide);
        let duration = pres
            .slides
            .get(self.state.slide)
            .and_then(|slide| slide.transition.as_ref())
            .filter(|tr| tr.kind != "none")
            .map_or(0., |tr| (tr.duration_ms as f64 / 1000.).clamp(0., 60.));
        let time = (self.step_clock.elapsed().as_secs_f64() - duration).max(0.);
        if self.state.playing && time >= timeline.step_duration(self.state.step) {
            self.state.step_done(pres);
        }
        if self
            .state
            .auto_advance_due(pres, self.slide_clock.elapsed().as_secs_f64())
        {
            self.state.next(pres);
            self.slide_clock = std::time::Instant::now();
            self.step_clock = std::time::Instant::now();
        }
        if self.state.ended {
            return Ok(serde_json::json!({"ended":true,"slide":self.state.slide,"steps":0}));
        }
        let timeline = deckcraft_anim::ShowState::timeline(pres, self.state.slide);
        let time = if self.state.playing {
            (self.step_clock.elapsed().as_secs_f64() - duration).max(0.)
        } else {
            0.
        };
        let state = |id| {
            let anim = self.state.anim_state(&timeline, id, time);
            deckcraft_render::ShapeState {
                visible: anim.visible,
                opacity: anim.opacity,
                offset: deckcraft_geom::Vec2::new(anim.offset_x, anim.offset_y),
                scale: (anim.scale_x, anim.scale_y),
                rotate: anim.rotate,
                clip: anim.clip,
                tint: anim.tint,
                paras: timeline
                    .paragraph_targets(id)
                    .into_iter()
                    .map(|index| {
                        let a = timeline.state(id, Some(index), self.state.step, time);
                        (
                            index,
                            deckcraft_render::ParaState {
                                visible: a.visible,
                                opacity: a.opacity,
                                offset: deckcraft_geom::Vec2::new(a.offset_x, a.offset_y),
                            },
                        )
                    })
                    .collect(),
            }
        };
        let mut image = deckcraft_render::render_slide(
            pres,
            self.state.slide,
            &deckcraft_render::RenderOpts {
                scale: 4. / 3.,
                state: Some(&state),
                ..Default::default()
            },
        );
        let elapsed = self.slide_clock.elapsed().as_secs_f64();
        if duration > 0. && elapsed < duration {
            let opts = deckcraft_render::RenderOpts {
                scale: 4. / 3.,
                state: Some(&state),
                ..Default::default()
            };
            image = transition_image(
                pres,
                self.transition_from,
                self.state.slide,
                elapsed / duration,
                &opts,
            );
        }
        Ok(
            serde_json::json!({"png":crate::cmd::base64_encode(&image.to_png()),"width":image.width,"height":image.height,"slide":self.state.slide,"step":self.state.step,"steps":timeline.steps(),"playing":self.state.playing,"ended":false}),
        )
    }
}

fn transition_image(
    pres: &deckcraft_model::Presentation,
    old: Option<usize>,
    new: usize,
    time: f64,
    opts: &deckcraft_render::RenderOpts<'_>,
) -> deckcraft_render::Image {
    let Some(slide) = pres.slides.get(new) else {
        return deckcraft_render::Image::default();
    };
    let Some(tr) = &slide.transition else {
        return deckcraft_render::render_slide(pres, new, opts);
    };
    if tr.kind == "morph" {
        if let Some((old_index, previous)) = old.and_then(|i| pres.slides.get(i).map(|s| (i, s))) {
            let morph = deckcraft_anim::morph_frame(pres, previous, slide, time, &|_| false);
            let state = |id| deckcraft_render::ShapeState {
                opacity: morph.opacity_of(id),
                ..Default::default()
            };
            let text: Vec<_> = morph
                .text
                .iter()
                .map(|t| deckcraft_render::TextMorph {
                    old: t.old,
                    new: t.new,
                    from: t.from,
                    to: t.to,
                    chars: t.chars,
                    t: morph.mix,
                })
                .collect();
            let paths: Vec<_> = morph
                .paths
                .iter()
                .map(|p| deckcraft_render::PathMorph {
                    id: p.id,
                    from: p.from.clone(),
                    from_box: p.from_box,
                    to_box: p.to_box,
                    t: morph.mix,
                })
                .collect();
            return deckcraft_render::render_blend(
                pres,
                (old_index, new),
                morph.mix,
                &morph.shapes,
                &text,
                &paths,
                &deckcraft_render::RenderOpts {
                    scale: opts.scale,
                    state: Some(&state),
                    ..Default::default()
                },
            );
        }
    }
    let incoming = deckcraft_render::render_slide(pres, new, opts);
    let outgoing = old.map(|i| deckcraft_render::render_slide(pres, i, opts));
    let parts = deckcraft_anim::uses_slide_parts(&tr.kind, &tr.option)
        .then(|| deckcraft_render::SlideParts::render(pres, old, new, opts));
    let layers = deckcraft_anim::transition_frame(
        &tr.kind,
        &tr.option,
        time,
        pres.slide_size.width / pres.slide_size.height.max(1.),
    );
    deckcraft_render::composite_transition(outgoing.as_ref(), &incoming, parts.as_ref(), &layers)
}
