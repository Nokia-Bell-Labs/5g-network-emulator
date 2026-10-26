function test_legacy_reproducibility()
%TEST_LEGACY_REPRODUCIBILITY Verify deterministic same-seed generation.

    root = tempname;
    first = fullfile(root, 'first');
    second = fullfile(root, 'second');
    cleanup = onCleanup(@() rmdir(root, 's'));

    first_path = generate_legacy_map( ...
        'URBAN_MICROCELL', 3.5, 42, first, 32);
    second_path = generate_legacy_map( ...
        'URBAN_MICROCELL', 3.5, 42, second, 32);

    first_bytes = fileread(first_path);
    second_bytes = fileread(second_path);
    assert(strcmp(first_bytes, second_bytes), ...
        'Same-seed legacy maps must be byte-identical');
end
