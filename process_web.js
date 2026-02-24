dfds
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

function main() {
    const args = process.argv.slice(2);

    if (args.length !== 2) {
        console.error("Usage: node process_web.js <source_web_dir> <binary_web_dir>");
        process.exit(1);
    }

    const srcDir = path.resolve(args[0]);
    const binDir = path.resolve(args[1]);

    // 1. Set up the isolated build workspace inside the binary folder
    const workspaceDir = path.join(binDir, "_workspace");
    const workspaceSrc = path.join(workspaceDir, "src");
    const distDir = path.join(binDir, "dist");

    console.log(`-- [Web Build] Creating workspace in: ${workspaceDir}`);
    fs.mkdirSync(workspaceDir, { recursive: true });

    if (fs.existsSync(workspaceSrc)) {
        fs.rmSync(workspaceSrc, { recursive: true, force: true });
    }
    if (fs.existsSync(distDir)) {
        fs.rmSync(distDir, { recursive: true, force: true });
    }

    // 2. Copy source files into the binary workspace safely
    // Note: fs.cpSync requires Node.js v16.7.0 or higher.
    fs.cpSync(srcDir, workspaceSrc, { recursive: true });

    // 3. Generate package.json
    const packageJson = {
        "name": "luxon-web-legacy-build",
        "version": "1.0.0",
        "private": true,
        "browserslist": ["IE 11"],
        "devDependencies": {
            "parcel": "^2.10.0",
            "postcss": "^8.4.0",
            "autoprefixer": "^10.4.0",
            "postcss-preset-env": "^9.0.0",
            "@babel/core": "^7.20.0",
            "@babel/preset-env": "^7.20.0"
        },
        "dependencies": {
            "core-js": "^3.30.0",
            "whatwg-fetch": "^3.6.0"
        }
    };

    fs.writeFileSync(
        path.join(workspaceDir, "package.json"),
        JSON.stringify(packageJson, null, 2)
    );

    // 5. Find all HTML files
    const htmlFiles = fs.readdirSync(workspaceSrc).filter(f => f.endsWith('.html'));

    if (htmlFiles.length === 0) {
        console.error("!! [Web Build] Error: No .html files found in the source directory.");
        process.exit(1);
    }

    console.log(`-- [Web Build] Found HTML files to process: ${htmlFiles.join(', ')}`);

    // 7. Run npm install
    console.log("-- [Web Build] Installing Node.js dependencies (this may take a minute)...");

    const isWin = process.platform === "win32";
    const npmCmd = isWin ? "npm.cmd" : "npm";

    // stdio: 'inherit' passes the output directly to the terminal, mirroring Python's default behavior
    const installProc = spawnSync(npmCmd, ["install"], { cwd: workspaceDir, stdio: 'inherit' });

    if (installProc.status !== 0) {
        console.error("!! [Web Build] Error: npm install failed.");
        process.exit(installProc.status !== null ? installProc.status : 1);
    }

    // 8. Run the bundler (Parcel) on ALL discovered HTML files
    console.log("-- [Web Build] Transpiling and bundling for legacy compatibility...");
    const npxCmd = isWin ? "npx.cmd" : "npx";

    // Construct the Parcel command with all HTML files as entry points
    const parcelArgs = ["parcel", "build"];
    htmlFiles.forEach(htmlFile => parcelArgs.push(`src/${htmlFile}`));
    parcelArgs.push("--dist-dir", distDir, "--public-url", "./", "--no-source-maps", "--no-cache");

    const buildProc = spawnSync(npxCmd, parcelArgs, { cwd: workspaceDir, stdio: 'inherit' });

    if (buildProc.status !== 0) {
        console.error("!! [Web Build] Error: Bundling failed.");
        process.exit(buildProc.status !== null ? buildProc.status : 1);
    }

    console.log("-- [Web Build] Normalizing CSS filename...");
    // Find the generated CSS file(s) in the dist folder
    const cssFiles = fs.readdirSync(distDir).filter(f => f.endsWith('.css'));

    if (cssFiles.length > 0) {
        // Grab the hashed name (e.g., _workspace.45e842fe.css)
        const hashedCssName = cssFiles[0];
        const cleanCssName = "style.css";

        // 1. Rename the actual .css file
        fs.renameSync(
            path.join(distDir, hashedCssName),
            path.join(distDir, cleanCssName)
        );

        // 2. Update the <link> tags in all generated HTML files
        const htmlOutputs = fs.readdirSync(distDir).filter(f => f.endsWith('.html'));
        for (const htmlFile of htmlOutputs) {
            const htmlPath = path.join(distDir, htmlFile);
            let htmlContent = fs.readFileSync(htmlPath, "utf-8");

            // Replace the hashed filename with our clean filename
            // Using split.join acts as a global replace for strings
            htmlContent = htmlContent.split(hashedCssName).join(cleanCssName);

            fs.writeFileSync(htmlPath, htmlContent, "utf-8");
        }

        console.log(`    Renamed: ${hashedCssName} -> ${cleanCssName}`);
    }

    console.log(`-- [Web Build] Success! Legacy files generated in: ${distDir}`);
    process.exit(0);
}

// Execute script
main();
