function output_path = generate_legacy_map( ...
    scenario, frequency_ghz, seed, output_directory, cell_number)
%GENERATE_LEGACY_MAP Deterministically reproduce the legacy map pipeline.
%
% This function intentionally preserves the current legacy generator
% semantics, including LOS smoothing and combination. It does not approve or
% correct those semantics. Production replacement maps require the supervised
% PHY Model V2 map-design review.

    arguments
        scenario (1, :) char
        frequency_ghz (1, 1) double {mustBePositive}
        seed (1, 1) double {mustBeInteger, mustBeNonnegative}
        output_directory (1, :) char
        cell_number (1, 1) double {mustBeInteger, mustBePositive} = 290
    end

    function_directory = fullfile(fileparts(mfilename('fullpath')), 'functions');
    addpath(function_directory);
    rng(seed, 'twister');

    [corr_los, corr_nlos, sigma_los, sigma_nlos] = ...
        adjust_parameters(scenario);
    cell_size = 0.5 * min(corr_los, corr_nlos);

    pathloss_los = generate_pathloss_map( ...
        scenario, true, cell_size, cell_number, frequency_ghz);
    pathloss_nlos = generate_pathloss_map( ...
        scenario, false, cell_size, cell_number, frequency_ghz);
    shadow_los = generate_filtered_matrix( ...
        corr_los, cell_size, sigma_los, cell_number);
    shadow_nlos = generate_filtered_matrix( ...
        corr_nlos, cell_size, sigma_nlos, cell_number);
    los_mask = map_LOS(cell_size, cell_number, scenario);

    macroscopic_los = shadow_los - pathloss_los;
    macroscopic_nlos = shadow_nlos - pathloss_nlos;

    % Preserve the historical MATLAB expression exactly. The floating-mask
    % semantics are a pending Block 2B decision.
    final_map = ...
        los_mask .* macroscopic_los ...
        + (~los_mask) .* macroscopic_nlos;

    if any(~isfinite(final_map(:)))
        error('Generated map contains non-finite values');
    end

    if ~exist(output_directory, 'dir')
        mkdir(output_directory);
    end
    output_name = sprintf( ...
        'macroscopic_fading_map_%s_%g_seed_%u.json', ...
        scenario, frequency_ghz, seed);
    output_path = fullfile(output_directory, output_name);

    payload.cell_number = cell_number;
    payload.cell_size = cell_size;
    payload.map = final_map;
    encoded = jsonencode(payload);
    file = fopen(output_path, 'w');
    if file == -1
        error('Could not open output file: %s', output_path);
    end
    cleanup = onCleanup(@() fclose(file));
    fwrite(file, encoded, 'char');
end
