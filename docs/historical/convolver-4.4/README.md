# Archived Convolver 4.4 website

This is the recovered historical website, preserved byte for byte from `homes.zip`. Its release history ends at **version 4.4**. The pages document the original Windows application, its plug-ins, configuration format, supported audio formats, utilities, and example setups. They are **historical documentation, not current installation or build instructions**. For the restored native streaming utility, use the [current convolverPipe guide](../../convolverPipe.md).

Start with the [archived home page](index.html), [usage and installation guide](usage.html), [configuration reference](config.html), [configuration examples](configegs.html), [command-line utilities](commands.html), or [release history](news.html). The [convolverPipe page](convolverpipe.html) includes the original SlimServer/SqueezeCenter instructions and links to its example configuration. The [VST guide](vst.html), [ZoomPlayer walkthrough](zoom.html), and [binaural demonstration](binaural.html) preserve the original host-specific guidance and illustrations.

The recovered files came from this ZIP member prefix (personal identity replaced with `jrp`):

```text
homes/DOMAIN=jrp/jrp/My Documents/My Web Sites/convolverpub/
```

The archive SHA-256 is `2b5cad28adcf5d8df21d6c9139941f4ab7607d2225ea29773642ee2beab74771`. The [documentation provenance manifest](../../review-2026-10/archive-documentation.json) records every recovered member path, byte count and SHA-256, the parallel website comparison, and the local/external link inventory. Filesystem and ZIP dates were not used to order or choose versions. The parallel `My Web Sites/convolver/` pages have the same text after removing markup and normalizing whitespace; the published `convolverpub/` copy supplies the complete static navigation and layout.

Recovery includes 18 documentation pages and their recursively referenced local assets: 99 original files, 1,696,463 bytes. The original `binaural.txt` and `custom-convert.conf` are examples that contain machine-specific paths and require adaptation. All local file references in the HTML and CSS resolve within the recovered site. Original fragment identifiers are checked separately in the manifest; any pre-existing mismatches are preserved. External URLs are historical and have not been checked. Original pages retain their external search form, SourceForge logo and Google Analytics script references; those require network access and are not necessary to read the local documentation.

FrontPage authoring/publishing metadata, the Google verification page, unreferenced assets, installer archives, executable binaries and unrelated audio/control libraries were excluded. `_derived/` navigation images and `_themes/` CSS/images were retained where referenced because the published pages need them to display correctly. No archived program, installer or command example was executed.

## Comparison with the published SourceForge site

On 17 October 2026, all 18 pages were retrieved from
[convolver.sourceforge.net](https://convolver.sourceforge.net/index.html) and
compared with this recovered copy. All visible text matched after whitespace
normalization; hyperlink/image references matched; preformatted examples matched
with line endings normalized. No additional same-site HTML documentation pages
were linked by the retrieved pages. HTML formatting and stylesheet references
differ, so the original archive bytes were retained. This checks content, not
filesystem timestamps or the old displayed modification dates.

The [comparison record](../../review-2026-10/sourceforge-documentation-check.json)
contains per-page URLs, hashes and results. Third-party linked destinations and
live image bytes were not audited as part of this content comparison.
