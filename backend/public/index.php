<?php
declare(strict_types=1);

if (!class_exists('App\ImageProcessor')) {
    require_once __DIR__ . '/../src/ImageProcessor.php';
}

$config = require __DIR__ . '/../config.php';

function sendJson(array $data, int $status = 200): void
{
    http_response_code($status);
    header('Content-Type: application/json');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function requireApiKey(array $config): void
{
    $expected = $config['api_key'] ?? '';
    $provided = $_SERVER['HTTP_X_API_KEY'] ?? null;
    if ($provided === null && $_SERVER['REQUEST_METHOD'] === 'POST') {
        $provided = $_POST['API_KEY'] ?? $_POST['api_key'] ?? null;
    }

    if (
        !is_string($expected)
        || $expected === ''
        || !is_string($provided)
        || !hash_equals($expected, $provided)
    ) {
        sendJson(['error' => 'Forbidden'], 403);
    }
}

function parameter(array $post, array $get, array $names): ?string
{
    foreach ($names as $name) {
        if (isset($post[$name]) && is_string($post[$name]) && $post[$name] !== '') {
            return $post[$name];
        }
    }
    foreach ($names as $name) {
        if (isset($get[$name]) && is_string($get[$name]) && $get[$name] !== '') {
            return $get[$name];
        }
    }
    return null;
}

function drawCenteredText(
    GdImage $image,
    string $text,
    int $color,
    int $scale,
    int $centerY
): void {
    $font = 5;
    $textWidth = imagefontwidth($font) * strlen($text);
    $textHeight = imagefontheight($font);
    $textImage = imagecreatetruecolor($textWidth, $textHeight);
    if ($textImage === false) {
        throw new RuntimeException('Could not create text image');
    }
    $white = imagecolorallocate($textImage, 255, 255, 255);
    imagefill($textImage, 0, 0, $white);
    imagestring($textImage, $font, 0, 0, $text, $color);

    $width = $textWidth * $scale;
    $height = $textHeight * $scale;
    $targetWidth = min(imagesx($image), $width);
    $targetHeight = min(imagesy($image), $height);
    imagecopyresized(
        $image,
        $textImage,
        intdiv(imagesx($image) - $targetWidth, 2),
        $centerY - intdiv($targetHeight, 2),
        0,
        0,
        $targetWidth,
        $targetHeight,
        $textWidth,
        $textHeight
    );
    imagedestroy($textImage);
}

function createExampleImage(
    string $path,
    int $contentId,
    int $width,
    int $height,
    array $colors
): void {
    $image = imagecreatetruecolor($width, $height);
    if ($image === false) {
        throw new RuntimeException('Could not create example image');
    }

    $white = imagecolorallocate($image, 255, 255, 255);
    $red = imagecolorallocate($image, 255, 0, 0);
    $black = imagecolorallocate($image, 0, 0, 0);
    $yellow = imagecolorallocate($image, 255, 255, 0);
    imagefill($image, 0, 0, $white);

    $weekday = $contentId === 1
        ? [
            1 => 'Montag',
            2 => 'Dienstag',
            3 => 'Mittwoch',
            4 => 'Donnerstag',
            5 => 'Freitag',
            6 => 'Samstag',
            7 => 'Sonntag',
        ][(int) date('N')]
        : date('l');
    $date = date('d.m.Y');

    $ratio = min($width / 800, $height / 480);
    $weekdayScale = max(1, (int) round(5 * $ratio));
    $dateScale = max(1, (int) round(4 * $ratio));
    $weekdayY = (int) round($height * 190 / 480);
    $dateY = (int) round($height * 300 / 480);

    if (count($colors) === 1) {
        $weekdayColor = match ($colors[0]) {
            'red' => $red,
            'yellow' => $yellow,
            default => $black,
        };
        $dateColor = $weekdayColor;
    } else {
        $weekdayColor = in_array('red', $colors, true) ? $red : $black;
        $dateColor = in_array('black', $colors, true) ? $black : $red;
    }

    drawCenteredText($image, $weekday, $weekdayColor, $weekdayScale, $weekdayY);
    drawCenteredText($image, $date, $dateColor, $dateScale, $dateY);

    if (!imagepng($image, $path)) {
        imagedestroy($image);
        throw new RuntimeException('Could not write example PNG');
    }
    imagedestroy($image);
}

function validateEpd2(string $path, int $width, int $height, int $colorCount): void
{
    $size = filesize($path);
    $expected = App\ImageProcessor::totalSize($width, $height, $colorCount);
    if ($size !== $expected) {
        throw new RuntimeException('Generated EPD file has an invalid size');
    }

    $handle = fopen($path, 'rb');
    if ($handle === false) {
        throw new RuntimeException('Could not read generated EPD file');
    }
    $header = fread($handle, 5);
    fclose($handle);
    if ($header === false || strlen($header) !== 5) {
        throw new RuntimeException('Generated EPD file has an invalid header');
    }
    $values = unpack('nwidth/nheight/Ccount', $header);
    if (
        $values === false
        || $values['width'] !== $width
        || $values['height'] !== $height
        || $values['count'] !== $colorCount
    ) {
        throw new RuntimeException('Generated EPD file has an invalid header');
    }
}

function ensureCacheDirectory(string $directory): void
{
    if (!is_dir($directory) && !mkdir($directory, 0777, true) && !is_dir($directory)) {
        throw new RuntimeException('Could not create cache directory');
    }
}

function safeBaseName(string $value): string
{
    return preg_replace('/[^a-zA-Z0-9.-]/', '-', $value) ?? 'invalid';
}

function ensureExampleImage(
    App\ImageProcessor $processor,
    array $config,
    int $contentId,
    int $width,
    int $height,
    array $colors
): array {
    $cacheDirectory = $config['cache_dir'];
    ensureCacheDirectory($cacheDirectory);
    $today = date('Y-m-d');
    $colorKey = hash('sha256', implode('|', $colors));
    $baseName = 'epd2-' . $contentId . '-' . $width . 'x' . $height . '-'
        . substr($colorKey, 0, 16) . '-' . $today;
    $pngPath = $cacheDirectory . '/' . safeBaseName($baseName) . '.png';
    $epdPath = $cacheDirectory . '/' . safeBaseName($baseName) . '.epd';

    if (is_file($epdPath)) {
        try {
            validateEpd2($epdPath, $width, $height, count($colors));
            return ['png' => $pngPath, 'epd' => $epdPath];
        } catch (RuntimeException) {
            // Regenerate an invalid cache entry.
        }
    }

    $suffix = bin2hex(random_bytes(6));
    $temporaryPng = $cacheDirectory . '/' . safeBaseName($baseName) . '-' . $suffix . '.tmp.png';
    $temporaryEpd = $cacheDirectory . '/' . safeBaseName($baseName) . '-' . $suffix . '.tmp.epd';

    try {
        createExampleImage($temporaryPng, $contentId, $width, $height, $colors);
        $processor->process($temporaryPng, $temporaryEpd, $width, $height, $colors);
        validateEpd2($temporaryEpd, $width, $height, count($colors));

        if (!rename($temporaryPng, $pngPath) || !rename($temporaryEpd, $epdPath)) {
            throw new RuntimeException('Could not publish image cache files');
        }
    } finally {
        foreach ([$temporaryPng, $temporaryEpd] as $temporaryPath) {
            if (is_file($temporaryPath)) {
                unlink($temporaryPath);
            }
        }
    }

    return ['png' => $pngPath, 'epd' => $epdPath];
}

function hashFile(string $path): string
{
    $handle = fopen($path, 'rb');
    if ($handle === false) {
        throw new RuntimeException('Could not open generated EPD file');
    }
    $context = hash_init('sha256');
    if (!hash_update_stream($context, $handle)) {
        fclose($handle);
        throw new RuntimeException('Could not hash generated EPD file');
    }
    fclose($handle);
    return hash_final($context);
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
$requestUri = $_SERVER['REQUEST_URI'] ?? '/';
$path = parse_url($requestUri, PHP_URL_PATH);
if (!is_string($path)) {
    $path = '/';
}
$post = $_POST;
$get = $_GET;
$processor = new App\ImageProcessor();

if (!in_array($method, ['GET', 'POST'], true)) {
    http_response_code(405);
    header('Allow: GET, POST');
    header('Content-Type: application/json');
    echo json_encode(['error' => 'Method not allowed']);
    exit;
}

if ($path === '/api/display/current') {
    requireApiKey($config);

    $contentValue = parameter($post, $get, ['CONTENT_ID', 'content_id']);
    $widthValue = parameter($post, $get, ['WIDTH', 'width', 'display_width']);
    $heightValue = parameter($post, $get, ['HEIGHT', 'height', 'display_height']);
    $colorsValue = parameter($post, $get, ['COLORS', 'colors', 'color_config', 'display_colors']);

    if (
        $contentValue === null
        || !preg_match('/^(0|[1-9][0-9]*)$/', $contentValue)
        || $widthValue === null
        || !preg_match('/^[1-9][0-9]*$/', $widthValue)
        || $heightValue === null
        || !preg_match('/^[1-9][0-9]*$/', $heightValue)
        || $colorsValue === null
    ) {
        sendJson(['error' => 'Bad Request'], 400);
    }

    $contentId = (int) $contentValue;
    $width = (int) $widthValue;
    $height = (int) $heightValue;
    if (!in_array($contentId, [0, 1], true)) {
        sendJson(['error' => 'Not Found'], 404);
    }
    if ($width > 65535 || $height > 65535) {
        sendJson(['error' => 'Bad Request'], 400);
    }

    $supportedDisplay = false;
    foreach (($config['supported_displays'] ?? []) as $display) {
        if (($display['width'] ?? null) === $width && ($display['height'] ?? null) === $height) {
            $supportedDisplay = true;
            break;
        }
    }
    if (!$supportedDisplay) {
        sendJson(['error' => 'Bad Request'], 400);
    }

    $colors = [];
    foreach (explode('|', $colorsValue) as $colorName) {
        $colorName = strtolower(trim($colorName));
        if ($colorName === '' || in_array($colorName, $colors, true)) {
            sendJson(['error' => 'Bad Request'], 400);
        }
        if (!in_array($colorName, $config['supported_colors'] ?? [], true)) {
            sendJson(['error' => 'Bad Request'], 400);
        }
        $colors[] = $colorName;
    }
    if (count($colors) > 255) {
        sendJson(['error' => 'Bad Request'], 400);
    }
    if (count($colors) === 1 && !in_array($colors[0], ['black', 'red'], true)) {
        sendJson(['error' => 'Bad Request'], 400);
    }

    $canonicalColors = implode('|', $colors);
    $canonical = 'content_id=' . $contentId
        . '|width=' . $width
        . '|height=' . $height
        . '|colors=' . $canonicalColors;
    $responseId = hash('sha256', $canonical);

    try {
        $cache = ensureExampleImage($processor, $config, $contentId, $width, $height, $colors);
        validateEpd2($cache['epd'], $width, $height, count($colors));
        $hash = hashFile($cache['epd']);
        $size = filesize($cache['epd']);
        if (!is_int($size)) {
            throw new RuntimeException('Could not determine generated EPD size');
        }

        $mappingDirectory = $config['cache_dir'] . '/responses';
        ensureCacheDirectory($mappingDirectory);
        $mappingPath = $mappingDirectory . '/' . $responseId . '.json';
        $mapping = [
            'response_id' => $responseId,
            'content_id' => $contentId,
            'width' => $width,
            'height' => $height,
            'colors' => $colors,
            'epd_file' => basename($cache['epd']),
        ];
        $temporaryMapping = $mappingPath . '.' . bin2hex(random_bytes(6)) . '.tmp';
        if (
            file_put_contents($temporaryMapping, json_encode($mapping, JSON_UNESCAPED_SLASHES)) === false
            || !rename($temporaryMapping, $mappingPath)
        ) {
            if (is_file($temporaryMapping)) {
                unlink($temporaryMapping);
            }
            throw new RuntimeException('Could not publish response mapping');
        }
    } catch (RuntimeException $exception) {
        error_log($exception->getMessage());
        sendJson(['error' => 'Internal Server Error'], 500);
    }

    sendJson([
        'hash' => $hash,
        'response_id' => $responseId,
        'width' => $width,
        'height' => $height,
        'format' => 'EPD2',
        'size' => $size,
        'url' => '/api/display/image',
    ]);
}

if ($path === '/api/display/image') {
    requireApiKey($config);

    $responseId = parameter($post, $get, ['RESPONSE_ID', 'response_id']);
    if (
        $responseId === null
        || !preg_match('/^[a-f0-9]{64}$/', $responseId)
    ) {
        sendJson(['error' => 'Not Found'], 404);
    }

    $mappingDirectory = $config['cache_dir'] . '/responses';
    $mappingPath = $mappingDirectory . '/' . $responseId . '.json';
    if (!is_file($mappingPath)) {
        sendJson(['error' => 'Not Found'], 404);
    }

    $mapping = json_decode((string) file_get_contents($mappingPath), true);
    if (
        !is_array($mapping)
        || ($mapping['response_id'] ?? null) !== $responseId
        || !is_string($mapping['epd_file'] ?? null)
        || !is_int($mapping['width'] ?? null)
        || !is_int($mapping['height'] ?? null)
        || !is_array($mapping['colors'] ?? null)
        || count($mapping['colors']) < 1
        || count($mapping['colors']) > 255
    ) {
        sendJson(['error' => 'Not Found'], 404);
    }

    $epdFile = basename($mapping['epd_file']);
    $epdPath = $config['cache_dir'] . '/' . $epdFile;
    if (!is_file($epdPath)) {
        sendJson(['error' => 'Not Found'], 404);
    }
    try {
        validateEpd2(
            $epdPath,
            (int) $mapping['width'],
            (int) $mapping['height'],
            count($mapping['colors'])
        );
        $hash = hashFile($epdPath);
        $size = filesize($epdPath);
        if (!is_int($size)) {
            throw new RuntimeException('Could not determine EPD size');
        }
    } catch (RuntimeException $exception) {
        error_log($exception->getMessage());
        sendJson(['error' => 'Internal Server Error'], 500);
    }

    $etag = '"' . $hash . '"';
    $matches = false;
    foreach (explode(',', $_SERVER['HTTP_IF_NONE_MATCH'] ?? '') as $candidate) {
        $candidate = trim($candidate);
        if ($candidate === '*' || $candidate === $etag || $candidate === $hash) {
            $matches = true;
            break;
        }
    }

    header('ETag: ' . $etag);
    if ($matches) {
        http_response_code(304);
        exit;
    }

    $handle = fopen($epdPath, 'rb');
    if ($handle === false) {
        sendJson(['error' => 'Internal Server Error'], 500);
    }
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . $size);
    fpassthru($handle);
    fclose($handle);
    exit;
}

if ($path === '/' && $method === 'GET') {
    header('Content-Type: text/html');
    echo '<!DOCTYPE html><html><body><h1>EPD2 Backend</h1><p>EPD2 image API for configurable displays.</p><p>Endpoints: <code>/api/display/current</code>, <code>/api/display/image</code></p></body></html>';
    exit;
}

http_response_code(404);
header('Content-Type: application/json');
echo json_encode(['error' => 'Not found']);
