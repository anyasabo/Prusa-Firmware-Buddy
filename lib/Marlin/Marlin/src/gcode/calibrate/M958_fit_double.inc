// EXPERIMENT: the shaper fitting of upstream master (7d8bf6858, M958.cpp lines 990-1246), in
// double, with no change. M958.cpp includes it in the namespace fit_double for an A/B on the printer.
static double get_inv_D(const input_shaper::Shaper &shaper) {
    double d = 0.;
    for (int i = 0; i < shaper.num_pulses; ++i) {
        d += static_cast<double>(shaper.a[i]);
    }
    return 1. / d;
}

/**
 * @brief Get vibration reduction
 *
 * called _estimate_shaper in klipper source
 *
 * @param shaper
 * @param system_damping_ratio
 * @param frequency
 * @return Vibration reduction for system with system_damping_ratio at frequency.
 */
static double vibration_reduction(const input_shaper::Shaper &shaper, float system_damping_ratio, float frequency) {
    const double inv_D = get_inv_D(shaper);
    const double omega = 2. * std::numbers::pi_v<double> * static_cast<double>(frequency);
    const double damping = static_cast<double>(system_damping_ratio) * omega;
    const double omega_d = omega * sqrt(1. - sq(static_cast<double>(system_damping_ratio)));

    double s = 0.;
    double c = 0.;

    for (int i = 0; i < shaper.num_pulses; ++i) {
        const double t_i = static_cast<double>(shaper.t[i]);
        const double w = shaper.a[i] * exp(-damping * (static_cast<double>(shaper.t[shaper.num_pulses - 1]) - t_i));
        s += w * sin(omega_d * t_i);
        c += w * cos(omega_d * t_i);
    }
    return (sqrt(sq(s) + sq(c)) * inv_D);
}

/**
 * @brief Get remaining vibrations
 *
 * called _estimate_remaining_vibrations in klipper source
 *
 * @param shaper
 * @param system_damping_ratio
 * @param psd power spectrum density
 *
 * @return remaining vibrations
 */
static float remaining_vibrations(const input_shaper::Shaper &shaper, float system_damping_ratio, const Spectrum &psd, float default_vibration_reduction) {
    float vibr_threshold = psd.max() / default_vibration_reduction;
    float remaining_vibrations_sum = 0.f;
    float all_vibrations_sum = 0.f;
    for (size_t i = 0; i < psd.size(); ++i) {
        const FrequencyGain fg = psd.get(i);
        all_vibrations_sum += max(fg.gain, 0.f);
        const float vibration = max(fg.gain * static_cast<float>(vibration_reduction(shaper, system_damping_ratio, fg.frequency)) - vibr_threshold, 0.f);
        remaining_vibrations_sum += vibration;
    }
    return (remaining_vibrations_sum / all_vibrations_sum);
}

/**
 * @brief Get shaper smoothing
 *
 * Called _get_shaper_smoothing in klipper source
 *
 * @param shaper
 * @return smoothing
 */
static float smoothing(const input_shaper::Shaper &shaper) {
    constexpr float accel = 5000.f;
    constexpr float scv = 5.f;
    constexpr float half_accel = accel / 2.f;

    const double inv_D = get_inv_D(shaper);

    double ts = 0.;
    for (int i = 0; i < shaper.num_pulses; ++i) {
        ts += static_cast<double>(shaper.a[i] * shaper.t[i]);
    }
    ts *= inv_D;

    double offset_90 = 0.;
    double offset_180 = 0.;

    for (int i = 0; i < shaper.num_pulses; ++i) {
        const double t_i = static_cast<double>(shaper.t[i]);
        const double a_i = static_cast<double>(shaper.a[i]);
        if (t_i >= ts) {
            /// Calculate offset for one of the axes
            offset_90 += a_i * (scv + half_accel * (t_i - ts)) * (t_i - ts);
        }
        offset_180 += a_i * half_accel * sq(t_i - ts);
    }
    offset_90 *= (inv_D * sqrt(2.));
    offset_180 *= inv_D;

    return static_cast<float>(max(offset_90, offset_180));
}

namespace {
    enum class Action {
        first,
        /// Find lowest vibrs, reverse order, return when maximum smoothing exceeded
        find_best_result = first,
        /// Try to find an 'optimal' shapper configuration: the one that is not
        /// much worse than the 'best' one, but gives much less smoothing
        select,
        last = select,
    };
    Action &operator++(Action &action) {
        using IntType = typename std::underlying_type<Action>::type;
        action = static_cast<Action>(static_cast<IntType>(action) + 1);
        return action;
    }
} // anonymous namespace

struct Shaper_result {
    float frequency;
    float score;
    float smoothing;
};
static Shaper_result fit_shaper(const FindBestShaperProgressHook &progress_hook, input_shaper::Type type, const Spectrum &psd, const Action final_action, input_shaper::AxisConfig default_config) {
    constexpr float start_frequency = 5.f;
    constexpr float end_frequency = 150.f;
    constexpr float frequency_step = .2f;
    constexpr float max_smoothing = std::numeric_limits<float>::max();

    const float default_damping_ratio = default_config.damping_ratio;
    const float default_vibration_reduction = default_config.vibration_reduction;

    struct Result {
        float frequency;
        float score;
        float smoothing;
        float vibrs;
    };

    Result best_result = { 0.f, 0.f, 0.f, std::numeric_limits<float>::max() };
    Result selected_result = { 0.f, 0.f, 0.f, std::numeric_limits<float>::max() };

    for (Action action = Action::first; action <= final_action; ++action) {
        for (float frequency = end_frequency; frequency >= start_frequency - epsilon; frequency -= frequency_step) {
            const float progress_ratio = (end_frequency - frequency) / (end_frequency - start_frequency);
            if (!progress_hook(type, progress_ratio).has_value()) {
                return {};
            }
            input_shaper::Shaper shaper = input_shaper::get(default_damping_ratio, frequency, default_vibration_reduction, type);
            const float shaper_smoothing = smoothing(shaper);
            if (Action::find_best_result == action && best_result.frequency && shaper_smoothing > max_smoothing) {
                Shaper_result result = { .frequency = best_result.frequency, .score = best_result.score, .smoothing = best_result.smoothing };
                return result;
            }
            /// Exact damping ratio of the printer is unknown, pessimizing
            /// remaining vibrations over possible damping values
            float shaper_vibrations = 0.f;
            for (float damping_ratio = 0.05f; damping_ratio <= 0.15f + epsilon; damping_ratio += 0.05f) {
                float vibrations = remaining_vibrations(shaper, damping_ratio, psd, default_vibration_reduction);
                if (vibrations > shaper_vibrations) {
                    shaper_vibrations = vibrations;
                }
                if (!progress_hook(type, progress_ratio).has_value()) {
                    return {};
                }
            }
            /// todo max_accel = self.find_shaper_max_accel(shaper) (not needed to fit filter)

            /// The score trying to minimize vibrations, but also accounting
            /// the growth of smoothing. The formula itself does not have any
            /// special meaning, it simply shows good results on real user data
            const float shaper_score = shaper_smoothing * (std::pow(shaper_vibrations, 1.5f) + shaper_vibrations * .2f + .01f);

            if (Action::find_best_result == action && shaper_vibrations < best_result.vibrs) {
                Result result = { .frequency = frequency, .score = shaper_score, .smoothing = shaper_smoothing, .vibrs = shaper_vibrations };
                best_result = result;
                selected_result = best_result;
            }

            if (Action::select == action && (shaper_vibrations < (best_result.vibrs * 1.1f)) && shaper_score < selected_result.score) {
                Result result = { .frequency = frequency, .score = shaper_score, .smoothing = shaper_smoothing, .vibrs = shaper_vibrations };
                selected_result = result;
            }
        }
        SERIAL_ECHO_START();
        SERIAL_ECHOPAIR("IS filter: ", to_string(type));
        switch (action) {
        case Action::find_best_result:
            SERIAL_ECHOPAIR(" lowest vibration freq: ", selected_result.frequency);
            break;
        case Action::select:
            SERIAL_ECHOPAIR(" selected freq: ", selected_result.frequency);
            break;
        }
        SERIAL_ECHO(" score: ");
        SERIAL_PRINT(selected_result.score, 6);
        SERIAL_ECHO(" remaining vibrations: ");
        SERIAL_PRINT(selected_result.vibrs, 8);
        SERIAL_ECHO(" smoothing: ");
        SERIAL_PRINTLN(selected_result.smoothing, 4);
    }

    Shaper_result shaper_result = { .frequency = selected_result.frequency, .score = selected_result.score, .smoothing = selected_result.smoothing };
    return shaper_result;
}

static input_shaper::AxisConfig find_best_shaper(const FindBestShaperProgressHook &progress_hook, const Spectrum &psd, const Action final_action, input_shaper::AxisConfig default_config) {
    struct Best_score {
        Shaper_result result;
        input_shaper::Type type;
    };
    std::optional<Best_score> best_shaper;

    for (input_shaper::Type shaper_type = input_shaper::Type::first; shaper_type <= input_shaper::Type::last; ++shaper_type) {
        if (shaper_type == input_shaper::Type::null || !std::ranges::contains(input_shaper::filter_list, shaper_type)) {
            continue;
        }

        if (!progress_hook(shaper_type, 0.0f).has_value()) {
            break;
        }

        Shaper_result shaper = fit_shaper(progress_hook, shaper_type, psd, final_action, default_config);

        if (
            !best_shaper.has_value() //
            || shaper.score * 1.2f < best_shaper->result.score
            || ((shaper.score * 1.05f < best_shaper->result.score) && (shaper.smoothing * 1.1f < best_shaper->result.smoothing)) //
        ) {
            best_shaper = {
                .result = shaper,
                .type = shaper_type,
            };
        }
    }

    return input_shaper::AxisConfig {
        .type = best_shaper ? best_shaper->type : input_shaper::Type::null,
        .frequency = best_shaper ? best_shaper->result.frequency : 0,
        .damping_ratio = default_config.damping_ratio,
        .vibration_reduction = default_config.vibration_reduction,
    };
}

static input_shaper::AxisConfig find_best_shaper(const Spectrum &psd, const Action final_action, input_shaper::AxisConfig default_config) {
    const auto progress_hook = [](input_shaper::Type, float) -> Result<void> {
        idle(true);
        return {};
    };
    return find_best_shaper(progress_hook, psd, final_action, default_config);
}

input_shaper::AxisConfig find_best_shaper(const FindBestShaperProgressHook &progress_hook, const Spectrum &psd, input_shaper::AxisConfig default_config) {
    return find_best_shaper(progress_hook, psd, Action::find_best_result, default_config);
}
