# CySynck documentation toolchain

The user manual is authored here and exported to `../Documentation/`. Only the complete generated export and its offline ZIP are distributed. This workspace, its dependencies, archives and build notes are not part of the reader.

## Regenerate and verify

Requirements: Node.js 22+ and npm. From the repository root:

```sh
npm ci --prefix .CyDocBuild
npm run build --prefix .CyDocBuild
npm test --prefix .CyDocBuild
```

`npm run check --prefix .CyDocBuild` runs the same validated clean export. As in CyDoc, check writes output; it is not a read-only lint. CMake runs `build.mjs` when manual inputs, public headers or configuration change. Both build helper scripts prepare npm dependencies before configuring CMake. Normal reading requires none of these tools.

The exporter builds in the owned `.CyDocBuild/.cache/export/`, validates it and replaces only an existing managed `Documentation/` export. It refuses an unrelated destination. Keep user notes/files outside the generated tree. The deterministic stored ZIP is `.CyDocBuild/.cache/CySynck-Documentation.zip`; Web transfers this authenticated archive, never hosts the reader. CMake copies the manual/archive next to build executables, installs them at the package root, and includes another complete copy inside macOS app Resources.

`distribution/README.md` and `README.fr.md` are the package's short user entry points, with working relative links to its installed reader. They also fit macOS Resources. The repository's contributor README files stay in Git and are not copied into binary packages with broken links to undistributed authoring inputs.

## Pinned CyDoc package

CyDoc is not currently published on npm. `vendor/cydoc-0.1.0.tgz` was built from the private repository using available Git authentication:

```sh
git clone https://github.com/MrMybal/CyDoc.git .CyDocBuild/.cache/CyDoc
git -C .CyDocBuild/.cache/CyDoc checkout e038c474577e1f305df3eba421f2b9c03b0f1d8d
cd .CyDocBuild/.cache/CyDoc
npm ci
npm pack --pack-destination ../../vendor
```

For an update, read CyDoc's `ai/README.md`, `ai/INTEGRATION.md`, `ai/AUTHORING.md` and `ai/API.md` at the chosen revision and inspect the built declarations. Rebuild the archive, update provenance/lockfile, review API translations and rerun the full checks. A normal application build does not need access to the private Git repository. npm's lockfile pins the build dependency integrity; runtime dependencies are bundled locally.

The only media is a compiler-generated static SVG architecture diagram. `modules: []` is deliberate: there are no source images, GIFs, videos, meshes, galleries or interactive graphs. Three.js is neither installed in this application toolchain nor included in the distribution. Upstream's separate development checkout can use its own development dependencies.

## Authoring and translations

`manual/en/` is the complete default edition; `manual/fr/` is the complete French edition. Use the same page IDs, parents and explicit ASCII heading IDs. Keep English `revision` and French `sourceRevision` synchronized only after reviewing the translated prose, labels and examples. Both editions must be complete before publication. Unsupported runtime locales fall back to English, preserving the topic and section.

`cydoc.config.json` selects the base reader, home, locales, aliases and only `include/cysynck` as its source root. Bilingual `@doc` blocks in the three distributed headers document the C++, C and plugin contracts. `apiIndex` creates include/symbol references without source bodies. It is a lexical reference index, not a C++ AST or Doxygen import. Private implementation, tests, Unreal example sources and dependencies are not indexed.

CyDoc 0.1's source extractor/index does not accept `sourceRevision` tags. `api-translations.json` therefore pins the exact reviewed English **and** French compiled revisions for those six API/include topics. A changed annotation/reference fails the application gate; review both editions before updating a pin to the revision shown in the candidate `.cache/export/DocData/manifest.json`. Do not declare an unreviewed translation current. The original CyDoc translation report remains intact; `application-translation-report.json` combines its authored-page status with this strict source pin gate. Both must have no missing/outdated pages; all framework-untracked source pages must have exact reviewed pins.

After changing the canonical CLI help literal, run:

```sh
node .CyDocBuild/update-cli.mjs
```

Both languages reproduce the actual English CLI output; translate surrounding instructions when behavior changes. The build refuses a stale command listing. `assets/web/help-contexts.json` is the shared native/Web context catalog; its targets are validated in both languages and compiled into the help helper. The CLI accepts stable page IDs, old `docs/name.md` paths and declared page aliases. Existing `docs/*.md` are small repository compatibility links, not a second shipped manual.

## Reader integration and checks

The official CyDoc CLI compiles/validates Markdown, annotations, diagrams, aliases and local resources. `reader.html`/`reader.mjs` are an application-owned embedding of the public `mountViewer` API, bundled as a classic IIFE, using the export's local `data.js` provider. No fetch, server, ESM loading at file origin or CDN is added. The host supplies the initial dark theme, persistent application theme and reading storage namespace. The reader owns keyboard/touch navigation, search, focus, bookmarks and reading positions. This edition has no optional media/enlargement controls.

Checks cover locale/anchor parity, explicit translation freshness, source pins, help routes, legacy inventory, local resource references, absence of media/Three.js, and exact CLI syntax. DOM-model tests render every topic in both languages, exercise fallback, context links, search keyboard, menu state and theme persistence without a network provider. They do not prove rendering or local-storage policy in a real browser. C++ tests verify file-URL encoding, Unicode/reserved paths, installed lookup, aliases, CLI print-only and refusal cases; package smoke checks verify the installed manual and complete downloadable ZIP, including relocated macOS bundle resources.

Actual `file://` browser opening, narrow layout, keyboard/touch interaction and file-origin storage should also be tested on Windows, macOS and Linux when the host automation permits it. Browser Use in the development environment blocks file URLs; do not bypass that policy. Headless Linux uses `cysynck docs PAGE --print`; a workstation can extract the archive and open its reader locally.
