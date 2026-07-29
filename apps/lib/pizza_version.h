/*
 * Copyright (c) 2026 jetpax <jetpax@users.noreply.github.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * The PiZZa release version, shown by every app that displays one.
 * Bump this single define when cutting a pizza-vX.Y.Z tag.
 *
 * This numbers the distribution -- the card image and everything on it.
 * The Arduino board package (arduino-core-vX.Y.Z, in
 * apps/Arduino/package_pizza_index.json) is versioned separately
 * because Boards Manager tracks it on its own cadence; PiZZa is kept
 * ahead of it so the two never read as competing.
 */

#ifndef PIZZA_VERSION_H_
#define PIZZA_VERSION_H_

#define PIZZA_VERSION "0.7.1"

#endif /* PIZZA_VERSION_H_ */
