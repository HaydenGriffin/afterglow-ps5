// Afterglow - The Home sheet: a Home Assistant glance with pressable tiles.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/home_sheet.hpp"
#include "afterglow/home_assistant.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/style.hpp"

namespace hui::afterglow
{
namespace
{
// Panel's sheet geometry until draw() reports the real area: the column
// beside the slab, under a title with a note, above the notice line.
constexpr gfx::Rect kDefaultArea{style::kColumn, 200, style::kSafeRight - style::kColumn, 736};
constexpr float kNoticeGap = 30;
constexpr float kNoticeSeconds = 6;
constexpr float kHeadlineSize = 48;

bool toggles(const HomeEntity &e)
{
    return e.controllable && e.domain != "scene" && e.domain != "script";
}
std::size_t entity_count(const HomeSnapshot &home)
{
    std::size_t count = 0;
    for (const auto &group : home.groups)
        count += group.entities.size();
    return count;
}
bool has_states(const HomeSnapshot &home)
{
    for (const auto &group : home.groups)
        for (const auto &e : group.entities)
            if (e.state != "—")
                return true;
    return false;
}
} // namespace

HomeSheet::HomeSheet() : area_(kDefaultArea)
{
    list_.set_bounds(area_);
}

void HomeSheet::open()
{
    overrides_.clear();
    notice_.clear();
    const auto &results = presentation().home.results;
    seen_result_ = results.empty() ? 0 : results.back().id;
    dirty_ = true;
    rebuild();
    list_.focus_first();
}

bool HomeSheet::back()
{
    return false;
}

bool HomeSheet::covers_screen() const
{
    return false;
}

std::string HomeSheet::note() const
{
    const auto &home = presentation().home;
    if (!home.configured)
        return "Not set up";
    switch (home.link)
    {
    case HomeLink::connected:
    {
        const auto count = entity_count(home);
        return "Connected · " + std::to_string(count) + (count == 1 ? " entity" : " entities");
    }
    case HomeLink::auth_failed:
        return "Token refused";
    case HomeLink::offline:
        return has_states(home) ? "Offline · last known states" : home.status;
    default:
        return "Connecting…";
    }
}

const HomeEntity *HomeSheet::entity(const std::string &entity_id) const
{
    for (const auto &group : presentation().home.groups)
        for (const auto &e : group.entities)
            if (e.entity_id == entity_id)
                return &e;
    return nullptr;
}

bool HomeSheet::showing_rows() const
{
    const auto &home = presentation().home;
    return home.configured && home.link != HomeLink::auth_failed &&
           (home.link == HomeLink::connected || has_states(home));
}

void HomeSheet::rebuild()
{
    const auto &home = presentation().home;
    std::vector<SheetRow> rows;
    row_entities_.clear();
    if (showing_rows())
        for (const auto &group : home.groups)
        {
            SheetRow heading;
            heading.item.title = group.title;
            heading.item.header = true;
            rows.push_back(std::move(heading));
            row_entities_.emplace_back();
            for (const auto &e : group.entities)
            {
                SheetRow row;
                row.item.title = e.name;
                bool on = e.on;
                const auto it = overrides_.find(e.entity_id);
                if (it != overrides_.end())
                    on = it->second.on;
                if (toggles(e) && e.available)
                {
                    row.chosen = on;
                    row.item.value = on ? "On" : "Off";
                }
                else
                    row.item.value = home_wire::with_unit(e.state, e.unit);
                // Unavailable rows read faint and refuse Cross, as Panel's do.
                row.item.disabled = !e.available;
                rows.push_back(std::move(row));
                row_entities_.push_back(e.entity_id);
            }
        }
    list_.set_rows(std::move(rows));
    built_revision_ = home.revision;
    dirty_ = false;
}

// Answers for presses: a refusal reverts the row and says why; success waits
// for the state itself (or the timeout) before the override goes.
void HomeSheet::read_results()
{
    auto &s = presentation();
    for (const auto &result : s.home.results)
    {
        if (result.id <= seen_result_)
            continue;
        seen_result_ = result.id;
        const HomeEntity *e = entity(result.entity_id);
        const std::string name = e != nullptr ? e->name : result.entity_id;
        if (!result.ok)
        {
            overrides_.erase(result.entity_id);
            notice_ = "Couldn’t " + std::string(e != nullptr && !toggles(*e) ? "run " : "switch ") +
                      name + (result.message.empty() ? "" : ": " + result.message);
            notice_age_ = 0;
            dirty_ = true;
        }
        else if (e != nullptr && !toggles(*e))
            s.show_toast(name);
    }
}

void HomeSheet::press(int row, ui::Feedback &feedback)
{
    auto &s = presentation();
    const auto at = static_cast<std::size_t>(row);
    const HomeEntity *target = at < row_entities_.size() ? entity(row_entities_[at]) : nullptr;
    if (target == nullptr || !target->controllable || !target->available ||
        s.home.link != HomeLink::connected)
    {
        if (target != nullptr && target->controllable && s.home.link != HomeLink::connected)
        {
            notice_ = "Home Assistant is offline";
            notice_age_ = 0;
        }
        feedback.play(audio::Cue::error);
        return;
    }
    HomeCommand command;
    command.entity_id = target->entity_id;
    if (toggles(*target))
    {
        const auto it = overrides_.find(target->entity_id);
        const bool on = it != overrides_.end() ? it->second.on : target->on;
        command.service = "toggle";
        overrides_[target->entity_id] = {!on, 0};
        feedback.play(audio::Cue::toggle);
    }
    else
        command.service = "turn_on";
    s.home_commands.push_back(std::move(command));
    notice_.clear();
    dirty_ = true;
}

void HomeSheet::set_area(const gfx::Rect &area)
{
    area_ = area;
}

void HomeSheet::update(const InputFrame &input, float dt, ui::Feedback &feedback,
                       bool reduced_motion)
{
    list_.set_reduced_motion(reduced_motion);
    const auto &home = presentation().home;
    list_.set_bounds(area_);
    read_results();
    for (auto it = overrides_.begin(); it != overrides_.end();)
    {
        it->second.age += dt;
        const HomeEntity *e = entity(it->first);
        // Confirmed by Home Assistant, or it never followed: show the truth.
        if (e == nullptr || e->on == it->second.on || it->second.age > kOptimisticSeconds)
        {
            it = overrides_.erase(it);
            dirty_ = true;
        }
        else
            ++it;
    }
    if (!notice_.empty() && (notice_age_ += dt) > kNoticeSeconds)
        notice_.clear();
    if (dirty_ || home.revision != built_revision_)
        rebuild();
    if (list_.handle(input, feedback) == ui::Event::activated && list_.focused() != nullptr)
        press(list_.focus(), feedback);
    if (dirty_)
        rebuild();
    list_.update(dt);
}

void HomeSheet::draw(ui::Canvas &canvas, float x, float y, float width, float height) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &home = presentation().home;
    if (showing_rows())
        list_.draw(canvas);
    else
    {
        std::string headline, body;
        if (!home.configured)
        {
            headline = "Connect Home Assistant";
            body = "Add ha_host, ha_token and ha_group lines to afterglow.conf, then reopen "
                   "Afterglow.";
        }
        else if (home.link == HomeLink::auth_failed)
        {
            headline = "Home Assistant refused the token";
            body = "Make a long-lived access token in your Home Assistant profile, put it in "
                   "ha_token, then reopen Afterglow.";
        }
        else if (home.link == HomeLink::offline)
        {
            headline = "Home Assistant is offline";
            body = home.status.empty() ? "Check ha_host and ha_port in afterglow.conf."
                                       : home.status + ". Check ha_host and ha_port.";
        }
        else
        {
            headline = "Connecting to Home Assistant";
            body = "The glance fills in as soon as it answers.";
        }
        ui::text(list, fonts.display, fonts.display.font->fit(headline, kHeadlineSize, width), x,
                 y + 88, kHeadlineSize, style::kPaper);
        ui::paragraph(list, fonts.regular, body, x, y + 140, style::kTypeLabel, width,
                      style::kTypeLabel * 1.35f, style::kMuted, 3);
    }
    if (!notice_.empty())
        ui::text(list, fonts.regular, fonts.regular.font->fit(notice_, style::kTypeMin, width), x,
                 y + height + kNoticeGap, style::kTypeMin, style::kPaper);
}
} // namespace hui::afterglow
