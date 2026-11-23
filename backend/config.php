<?php
/**
 * Configuration for the EPD2 backend.
 * Example backend configuration.
 */

return [
    // Generated PNG and EPD data are cached here.
    'cache_dir' => __DIR__ . '/storage',

    // Send this value in the X-API-Key header. Replace it for deployment.
    'api_key' => '00000000000000000000000000000000',

    // Display geometries and logical color names accepted by this example.
    'supported_displays' => [
        ['width' => 800, 'height' => 480],
        ['width' => 10, 'height' => 10],
    ],
    'supported_colors' => ['white', 'black', 'red', 'yellow'],
    'dithering' => false,
];