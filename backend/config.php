<?php
/**
 * Configuration for the EPD2 backend.
 * Example backend configuration.
 */

return [
    // Generated PNG, EPD data, and date marker are cached here.
    'cache_dir' => __DIR__ . '/storage',

    // Send this value in the X-API-Key header. Replace it for deployment.
    'api_key' => '00000000000000000000000000000000',
];