#pragma once

enum class harq_model
{
    disabled,
    legacy_bler
};

inline const char *harq_model_name(harq_model model)
{
    return model == harq_model::legacy_bler
               ? "legacy_bler"
               : "disabled";
}
