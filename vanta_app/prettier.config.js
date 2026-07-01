/**
 * Prettier configuration for the Vanta app.
 *
 * Prettier is an opinionated code formatter. It automatically reformats code
 * so the team does not spend time debating style. This config is intentionally
 * small: it uses Prettier's defaults with a few project-wide tweaks.
 */
module.exports = {
  // Use single quotes for strings to match the existing JavaScript style.
  singleQuote: true,
  // Add trailing commas where valid in ES2017 (e.g., arrays, objects, function params).
  trailingComma: 'es5',
  // 2-space indentation to match the existing source files.
  tabWidth: 2,
  // 100 columns keeps lines readable on laptops while avoiding excessive wrapping.
  printWidth: 100,
  // Do not format Markdown or YAML in this project; focus on JS/TS code.
  overrides: [
    {
      files: '*.json',
      options: {
        // JSON cannot have trailing commas.
        trailingComma: 'none',
      },
    },
  ],
};
