/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com> */

#ifndef __LIBGPIOD_GPIOD_H__
#define __LIBGPIOD_GPIOD_H__

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gpiod.h
 */

/**
 * @mainpage libgpiod public API
 *
 * This is the complete documentation of the public API made available to
 * users of libgpiod.
 *
 * <p>The API is logically split into several parts such as: GPIO chip & line
 * operators, GPIO events handling etc.
 *
 * <p>General note on error handling: all routines exported by libgpiod  set
 * errno to one of the error values defined in errno.h upon failure. The way
 * of notifying the caller that an error occurred varies between functions,
 * but in general a function that returns an int, returns -1 on error, while
 * a function returning a pointer bails out on error condition by returning
 * a NULL pointer.
 */

struct gpiod_chip;
struct gpiod_line;
struct gpiod_line_bulk;

/**
 * @defgroup common Common helper macros
 * @{
 *
 * Commonly used utility macros.
 */

/**
 * @brief Shift 1 by given offset.
 * @param nr Bit position.
 * @return 1 shifted by nr.
 */
#define GPIOD_BIT(nr)		(1UL << (nr))

/**
 * @}
 *
 * @defgroup chips GPIO chip operations
 * @{
 *
 * Functions and data structures dealing with GPIO chips.
 */

/**
 * @brief Check if the file pointed to by path is a GPIO chip character device.
 * @param path Path to check.
 * @return True if the file exists and is a GPIO chip character device or a
 *         symbolic link to it.
 */
bool gpiod_is_gpiochip_device(const char *path);

/**
 * @brief Open a gpiochip by path.
 * @param path Path to the gpiochip device file.
 * @return GPIO chip handle or NULL if an error occurred.
 */
struct gpiod_chip *gpiod_chip_open(const char *path);

/**
 * @brief Increase the refcount on this GPIO object.
 * @param chip The GPIO chip object.
 * @return Passed reference to the GPIO chip.
 */
struct gpiod_chip *gpiod_chip_ref(struct gpiod_chip *chip);

/**
 * @brief Decrease the refcount on this GPIO object. If the refcount reaches 0,
 *        close the chip device and free all associated resources.
 * @param chip The GPIO chip object.
 */
void gpiod_chip_unref(struct gpiod_chip *chip);

/**
 * @brief Get the GPIO chip name as represented in the kernel.
 * @param chip The GPIO chip object.
 * @return Pointer to a human-readable string containing the chip name.
 */
const char *gpiod_chip_name(struct gpiod_chip *chip);

/**
 * @brief Get the GPIO chip label as represented in the kernel.
 * @param chip The GPIO chip object.
 * @return Pointer to a human-readable string containing the chip label.
 */
const char *gpiod_chip_label(struct gpiod_chip *chip);

/**
 * @brief Get the number of GPIO lines exposed by this chip.
 * @param chip The GPIO chip object.
 * @return Number of GPIO lines.
 */
unsigned int gpiod_chip_num_lines(struct gpiod_chip *chip);

/**
 * @brief Get the handle to the GPIO line at given offset.
 * @param chip The GPIO chip object.
 * @param offset The offset of the GPIO line.
 * @return Pointer to the GPIO line handle or NULL if an error occured.
 */
struct gpiod_line *
gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset);

/**
 * @brief Retrieve a set of lines and store them in a line bulk object.
 * @param chip The GPIO chip object.
 * @param offsets Array of offsets of lines to retrieve.
 * @param num_offsets Number of lines to retrieve.
 * @return New line bulk object or NULL on error.
 */
struct gpiod_line_bulk *
gpiod_chip_get_lines(struct gpiod_chip *chip, unsigned int *offsets,
		     unsigned int num_offsets);

/**
 * @brief Retrieve all lines exposed by a chip and store them in a bulk object.
 * @param chip The GPIO chip object.
 * @return New line bulk object or NULL on error.
 */
struct gpiod_line_bulk *
gpiod_chip_get_all_lines(struct gpiod_chip *chip);

/**
 * @brief Map a GPIO line's name to its offset within the chip.
 * @param chip The GPIO chip object.
 * @param name Name of the GPIO line to map.
 * @return Offset of the line within the chip or -1 if a line with given name
 *         is not exposed by the chip.
 */
int gpiod_chip_find_line(struct gpiod_chip *chip, const char *name);

/**
 * @}
 *
 * @defgroup lines GPIO line operations
 * @{
 *
 * Functions and data structures dealing with GPIO lines.
 *
 * @defgroup line_bulk Operating on multiple lines
 * @{
 *
 * Convenience data structures and helper functions for storing and operating
 * on multiple lines at once.
 */

/**
 * @brief Allocate and initialize a new line bulk object.
 * @param max_lines Maximum number of lines this object can hold.
 * @return New line bulk object or NULL on error.
 */
struct gpiod_line_bulk *gpiod_line_bulk_new(unsigned int max_lines);

/**
 * @brief Reset a bulk object. Remove all lines and set size to 0.
 * @param bulk Bulk object to reset.
 */
void gpiod_line_bulk_reset(struct gpiod_line_bulk *bulk);

/**
 * @brief Release all resources allocated for this bulk object.
 * @param bulk Bulk object to free.
 */
void gpiod_line_bulk_free(struct gpiod_line_bulk *bulk);

/**
 * @brief Add a single line to a GPIO bulk object.
 * @param bulk Line bulk object.
 * @param line Line to add.
 * @return 0 on success, -1 on error.
 * @note The line is added at the next free bulk index.
 *
 * The function can fail if this bulk already holds its maximum amount of
 * lines or if the added line is associated with a different chip than all
 * the other lines already held by this object.
 */
int gpiod_line_bulk_add_line(struct gpiod_line_bulk *bulk,
			     struct gpiod_line *line);

/**
 * @brief Retrieve the line handle from a line bulk object at given index.
 * @param bulk Line bulk object.
 * @param index Index of the line to retrieve.
 * @return Line handle at given index or NULL if index is greater or equal to
 *         the number of lines this bulk can hold.
 */
struct gpiod_line *
gpiod_line_bulk_get_line(struct gpiod_line_bulk *bulk, unsigned int index);

/**
 * @brief Retrieve the number of GPIO lines held by this line bulk object.
 * @param bulk Line bulk object.
 * @return Number of lines held by this line bulk.
 */
unsigned int gpiod_line_bulk_num_lines(struct gpiod_line_bulk *bulk);

/**
 * @brief Values returned by the callback passed to
 *        ::gpiod_line_bulk_foreach_line.
 */
enum {
	/**< Continue the loop. */
	GPIOD_LINE_BULK_CB_NEXT = 0,
	/**< Stop the loop. */
	GPIOD_LINE_BULK_CB_STOP,
};

/**
 * @brief Signature of the callback passed to ::gpiod_line_bulk_foreach_line.
 *
 * Takes the current line and additional user data as arguments.
 */
typedef int (*gpiod_line_bulk_foreach_cb)(struct gpiod_line *, void *);

/**
 * @brief Iterate over all lines held by this bulk object.
 * @param bulk Bulk object to iterate over.
 * @param func Callback to be called for each line.
 * @param data User data pointer that is passed to the callback.
 */
void gpiod_line_bulk_foreach_line(struct gpiod_line_bulk *bulk,
				  gpiod_line_bulk_foreach_cb func,
				  void *data);

/**
 * @}
 *
 * @defgroup line_info Line info
 * @{
 *
 * Definitions and functions for retrieving kernel information about both
 * requested and free lines.
 */

/**
 * @brief Possible direction settings.
 */
enum {
	GPIOD_LINE_DIRECTION_INPUT = 1,
	/**< Direction is input - we're reading the state of a GPIO line. */
	GPIOD_LINE_DIRECTION_OUTPUT,
	/**< Direction is output - we're driving the GPIO line. */
};

/**
 * @brief Possible drive settings.
 */
enum {
	GPIOD_LINE_DRIVE_PUSH_PULL = 1,
	/**< Drive setting is push-pull. */
	GPIOD_LINE_DRIVE_OPEN_DRAIN,
	/**< Line output is open-drain. */
	GPIOD_LINE_DRIVE_OPEN_SOURCE,
	/**< Line output is open-source. */
};

/**
 * @brief Possible internal bias settings.
 */
enum {
	GPIOD_LINE_BIAS_UNKNOWN = 1,
	/**< The internal bias state is unknown. */
	GPIOD_LINE_BIAS_DISABLED,
	/**< The internal bias is disabled. */
	GPIOD_LINE_BIAS_PULL_UP,
	/**< The internal pull-up bias is enabled. */
	GPIOD_LINE_BIAS_PULL_DOWN,
	/**< The internal pull-down bias is enabled. */
};

/**
 * @brief Read the GPIO line offset.
 * @param line GPIO line object.
 * @return Line offset.
 */
unsigned int gpiod_line_offset(struct gpiod_line *line);

/**
 * @brief Read the GPIO line name.
 * @param line GPIO line object.
 * @return Name of the GPIO line as it is represented in the kernel. This
 *         routine returns a pointer to a null-terminated string or NULL if
 *         the line is unnamed.
 */
const char *gpiod_line_name(struct gpiod_line *line);

/**
 * @brief Read the GPIO line consumer name.
 * @param line GPIO line object.
 * @return Name of the GPIO consumer name as it is represented in the
 *         kernel. This routine returns a pointer to a null-terminated string
 *         or NULL if the line is not used.
 */
const char *gpiod_line_consumer(struct gpiod_line *line);

/**
 * @brief Read the GPIO line direction setting.
 * @param line GPIO line object.
 * @return Returns GPIOD_LINE_DIRECTION_INPUT or GPIOD_LINE_DIRECTION_OUTPUT.
 */
int gpiod_line_direction(struct gpiod_line *line);

/**
 * @brief Check if the signal of this line is inverted.
 * @param line GPIO line object.
 * @return True if this line is "active-low", false otherwise.
 */
bool gpiod_line_is_active_low(struct gpiod_line *line);

/**
 * @brief Read the GPIO line bias setting.
 * @param line GPIO line object.
 * @return Returns GPIOD_LINE_BIAS_PULL_UP, GPIOD_LINE_BIAS_PULL_DOWN,
 *         GPIOD_LINE_BIAS_DISABLE or GPIOD_LINE_BIAS_UNKNOWN.
 */
int gpiod_line_bias(struct gpiod_line *line);

/**
 * @brief Check if the line is currently in use.
 * @param line GPIO line object.
 * @return True if the line is in use, false otherwise.
 *
 * The user space can't know exactly why a line is busy. It may have been
 * requested by another process or hogged by the kernel. It only matters that
 * the line is used and we can't request it.
 */
bool gpiod_line_is_used(struct gpiod_line *line);

/**
 * @brief Read the GPIO line drive setting.
 * @param line GPIO line object.
 * @return Returns GPIOD_LINE_DRIVE_PUSH_PULL, GPIOD_LINE_DRIVE_OPEN_DRAIN or
 *         GPIOD_LINE_DRIVE_OPEN_SOURCE.
 */
int gpiod_line_drive(struct gpiod_line *line);

/**
 * @brief Get the handle to the GPIO chip controlling this line.
 * @param line The GPIO line object.
 * @return Pointer to the GPIO chip handle controlling this line.
 */
struct gpiod_chip *gpiod_line_get_chip(struct gpiod_line *line);

/**
 * @}
 *
 * @defgroup line_request Line requests
 * @{
 *
 * Interface for requesting GPIO lines from userspace for both values and
 * events.
 */

/**
 * @brief Available types of requests.
 */
enum {
	GPIOD_LINE_REQUEST_DIRECTION_AS_IS = 1,
	/**< Request the line(s), but don't change current direction. */
	GPIOD_LINE_REQUEST_DIRECTION_INPUT,
	/**< Request the line(s) for reading the GPIO line state. */
	GPIOD_LINE_REQUEST_DIRECTION_OUTPUT,
	/**< Request the line(s) for setting the GPIO line state. */
	GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE,
	/**< Only watch falling edge events. */
	GPIOD_LINE_REQUEST_EVENT_RISING_EDGE,
	/**< Only watch rising edge events. */
	GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES,
	/**< Monitor both types of events. */
};

/**
 * @brief Miscellaneous GPIO request flags.
 */
enum {
	GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN	= GPIOD_BIT(0),
	/**< The line is an open-drain port. */
	GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE	= GPIOD_BIT(1),
	/**< The line is an open-source port. */
	GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW	= GPIOD_BIT(2),
	/**< The active state of the line is low (high is the default). */
	GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLED	= GPIOD_BIT(3),
	/**< The line has neither either pull-up nor pull-down resistor. */
	GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN	= GPIOD_BIT(4),
	/**< The line has pull-down resistor enabled. */
	GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP	= GPIOD_BIT(5),
	/**< The line has pull-up resistor enabled. */
};

/**
 * @brief Structure holding configuration of a line request.
 */
struct gpiod_line_request_config {
	const char *consumer;
	/**< Name of the consumer. */
	int request_type;
	/**< Request type. */
	int flags;
	/**< Other configuration flags. */
};

/**
 * @brief Reserve a single line.
 * @param line GPIO line object.
 * @param config Request options.
 * @param default_val Initial line value - only relevant if we're setting
 *                    the direction to output.
 * @return 0 if the line was properly reserved. In case of an error this
 *         routine returns -1 and sets the last error number.
 *
 * If this routine succeeds, the caller takes ownership of the GPIO line until
 * it's released.
 */
int gpiod_line_request(struct gpiod_line *line,
		       const struct gpiod_line_request_config *config,
		       int default_val);

/**
 * @brief Reserve a single line, set the direction to input.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @return 0 if the line was properly reserved, -1 on failure.
 */
int gpiod_line_request_input(struct gpiod_line *line, const char *consumer);

/**
 * @brief Reserve a single line, set the direction to output.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param default_val Initial line value.
 * @return 0 if the line was properly reserved, -1 on failure.
 */
int gpiod_line_request_output(struct gpiod_line *line,
			      const char *consumer, int default_val);

/**
 * @brief Request rising edge event notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_rising_edge_events(struct gpiod_line *line,
					  const char *consumer);

/**
 * @brief Request falling edge event notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_falling_edge_events(struct gpiod_line *line,
					   const char *consumer);

/**
 * @brief Request all event type notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_both_edges_events(struct gpiod_line *line,
					 const char *consumer);

/**
 * @brief Reserve a single line, set the direction to input.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the line was properly reserved, -1 on failure.
 */
int gpiod_line_request_input_flags(struct gpiod_line *line,
				   const char *consumer, int flags);

/**
 * @brief Reserve a single line, set the direction to output.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @param default_val Initial line value.
 * @return 0 if the line was properly reserved, -1 on failure.
 */
int gpiod_line_request_output_flags(struct gpiod_line *line,
				    const char *consumer, int flags,
				    int default_val);

/**
 * @brief Request rising edge event notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_rising_edge_events_flags(struct gpiod_line *line,
						const char *consumer,
						int flags);

/**
 * @brief Request falling edge event notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_falling_edge_events_flags(struct gpiod_line *line,
						 const char *consumer,
						 int flags);

/**
 * @brief Request all event type notifications on a single line.
 * @param line GPIO line object.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_both_edges_events_flags(struct gpiod_line *line,
					       const char *consumer,
					       int flags);

/**
 * @brief Reserve a set of GPIO lines.
 * @param bulk Set of GPIO lines to reserve.
 * @param config Request options.
 * @param default_vals Initial line values - only relevant if we're setting
 *                     the direction to output.
 * @return 0 if all lines were properly requested. In case of an error
 *         this routine returns -1 and sets the last error number.
 *
 * If this routine succeeds, the caller takes ownership of the GPIO lines
 * until they're released. All the requested lines must be provided by the
 * same gpiochip.
 */
int gpiod_line_request_bulk(struct gpiod_line_bulk *bulk,
			    const struct gpiod_line_request_config *config,
			    const int *default_vals);

/**
 * @brief Reserve a set of GPIO lines, set the direction to input.
 * @param bulk Set of GPIO lines to reserve.
 * @param consumer Name of the consumer.
 * @return 0 if the lines were properly reserved, -1 on failure.
 */
int gpiod_line_request_bulk_input(struct gpiod_line_bulk *bulk,
				  const char *consumer);

/**
 * @brief Reserve a set of GPIO lines, set the direction to output.
 * @param bulk Set of GPIO lines to reserve.
 * @param consumer Name of the consumer.
 * @param default_vals Initial line values.
 * @return 0 if the lines were properly reserved, -1 on failure.
 */
int gpiod_line_request_bulk_output(struct gpiod_line_bulk *bulk,
				   const char *consumer,
				   const int *default_vals);

/**
 * @brief Request rising edge event notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_rising_edge_events(struct gpiod_line_bulk *bulk,
					       const char *consumer);

/**
 * @brief Request falling edge event notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_falling_edge_events(struct gpiod_line_bulk *bulk,
						const char *consumer);

/**
 * @brief Request all event type notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_both_edges_events(struct gpiod_line_bulk *bulk,
					      const char *consumer);

/**
 * @brief Reserve a set of GPIO lines, set the direction to input.
 * @param bulk Set of GPIO lines to reserve.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the lines were properly reserved, -1 on failure.
 */
int gpiod_line_request_bulk_input_flags(struct gpiod_line_bulk *bulk,
					const char *consumer, int flags);

/**
 * @brief Reserve a set of GPIO lines, set the direction to output.
 * @param bulk Set of GPIO lines to reserve.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @param default_vals Initial line values.
 * @return 0 if the lines were properly reserved, -1 on failure.
 */
int gpiod_line_request_bulk_output_flags(struct gpiod_line_bulk *bulk,
					 const char *consumer, int flags,
					 const int *default_vals);

/**
 * @brief Request rising edge event notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_rising_edge_events_flags(
					struct gpiod_line_bulk *bulk,
					const char *consumer, int flags);

/**
 * @brief Request falling edge event notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_falling_edge_events_flags(
					struct gpiod_line_bulk *bulk,
					const char *consumer, int flags);

/**
 * @brief Request all event type notifications on a set of lines.
 * @param bulk Set of GPIO lines to request.
 * @param consumer Name of the consumer.
 * @param flags Additional request flags.
 * @return 0 if the operation succeeds, -1 on failure.
 */
int gpiod_line_request_bulk_both_edges_events_flags(
					struct gpiod_line_bulk *bulk,
					const char *consumer, int flags);

/**
 * @brief Release a previously reserved line.
 * @param line GPIO line object.
 */
void gpiod_line_release(struct gpiod_line *line);

/**
 * @brief Release a set of previously reserved lines.
 * @param bulk Set of GPIO lines to release.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
void gpiod_line_release_bulk(struct gpiod_line_bulk *bulk);

/**
 * @}
 *
 * @defgroup line_value Reading & setting line values
 * @{
 *
 * Functions allowing to read and set GPIO line values for single lines and
 * in bulk.
 */

/**
 * @brief Read current value of a single GPIO line.
 * @param line GPIO line object.
 * @return 0 or 1 if the operation succeeds. On error this routine returns -1
 *         and sets the last error number.
 */
int gpiod_line_get_value(struct gpiod_line *line);

/**
 * @brief Read current values of a set of GPIO lines.
 * @param bulk Set of GPIO lines to reserve.
 * @param values An array big enough to hold line_bulk->num_lines values.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If succeeds, this routine fills the values array with a set of values in
 * the same order, the lines are added to line_bulk. If the lines were not
 * previously requested together, the behavior is undefined.
 */
int gpiod_line_get_value_bulk(struct gpiod_line_bulk *bulk, int *values);

/**
 * @brief Set the value of a single GPIO line.
 * @param line GPIO line object.
 * @param value New value.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 */
int gpiod_line_set_value(struct gpiod_line *line, int value);

/**
 * @brief Set the values of a set of GPIO lines.
 * @param bulk Set of GPIO lines to reserve.
 * @param values An array holding line_bulk->num_lines new values for lines.
 *               A NULL pointer is interpreted as a logical low for all lines.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
int gpiod_line_set_value_bulk(struct gpiod_line_bulk *bulk, const int *values);

/**
 * @}
 *
 * @defgroup line_config Setting line configuration
 * @{
 *
 * Functions allowing modification of config options of GPIO lines requested
 * from user-space.
 */

/**
 * @brief Update the configuration of a single GPIO line.
 * @param line GPIO line object.
 * @param direction Updated direction which may be one of
 *                  GPIOD_LINE_REQUEST_DIRECTION_AS_IS,
 *                  GPIOD_LINE_REQUEST_DIRECTION_INPUT, or
 *                  GPIOD_LINE_REQUEST_DIRECTION_OUTPUT.
 * @param flags Replacement flags.
 * @param value The new output value for the line when direction is
 *              GPIOD_LINE_REQUEST_DIRECTION_OUTPUT.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 */
int gpiod_line_set_config(struct gpiod_line *line, int direction,
			  int flags, int value);

/**
 * @brief Update the configuration of a set of GPIO lines.
 * @param bulk Set of GPIO lines.
 * @param direction Updated direction which may be one of
 *                  GPIOD_LINE_REQUEST_DIRECTION_AS_IS,
 *                  GPIOD_LINE_REQUEST_DIRECTION_INPUT, or
 *                  GPIOD_LINE_REQUEST_DIRECTION_OUTPUT.
 * @param flags Replacement flags.
 * @param values An array holding line_bulk->num_lines new logical values
 *               for lines when direction is
 *               GPIOD_LINE_REQUEST_DIRECTION_OUTPUT.
 *               A NULL pointer is interpreted as a logical low for all lines.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
int gpiod_line_set_config_bulk(struct gpiod_line_bulk *bulk,
			       int direction, int flags, const int *values);


/**
 * @brief Update the configuration flags of a single GPIO line.
 * @param line GPIO line object.
 * @param flags Replacement flags.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 */
int gpiod_line_set_flags(struct gpiod_line *line, int flags);

/**
 * @brief Update the configuration flags of a set of GPIO lines.
 * @param bulk Set of GPIO lines.
 * @param flags Replacement flags.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
int gpiod_line_set_flags_bulk(struct gpiod_line_bulk *bulk, int flags);

/**
 * @brief Set the direction of a single GPIO line to input.
 * @param line GPIO line object.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 */
int gpiod_line_set_direction_input(struct gpiod_line *line);

/**
 * @brief Set the direction of a set of GPIO lines to input.
 * @param bulk Set of GPIO lines.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
int
gpiod_line_set_direction_input_bulk(struct gpiod_line_bulk *bulk);

/**
 * @brief Set the direction of a single GPIO line to output.
 * @param line GPIO line object.
 * @param value The logical value output on the line.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 */
int gpiod_line_set_direction_output(struct gpiod_line *line, int value);

/**
 * @brief Set the direction of a set of GPIO lines to output.
 * @param bulk Set of GPIO lines.
 * @param values An array holding line_bulk->num_lines new logical values
 *               for lines.  A NULL pointer is interpreted as a logical low
 *               for all lines.
 * @return 0 is the operation succeeds. In case of an error this routine
 *         returns -1 and sets the last error number.
 *
 * If the lines were not previously requested together, the behavior is
 * undefined.
 */
int gpiod_line_set_direction_output_bulk(struct gpiod_line_bulk *bulk,
					 const int *values);

/**
 * @}
 *
 * @defgroup line_event Line events handling
 * @{
 *
 * Structures and functions allowing to poll lines for events and read them,
 * both for individual lines as well as in bulk. Also contains functions for
 * retrieving the associated file descriptors and operate on them for easy
 * integration with standard unix interfaces.
 */

/**
 * @brief Event types.
 */
enum {
	GPIOD_LINE_EVENT_RISING_EDGE = 1,
	/**< Rising edge event. */
	GPIOD_LINE_EVENT_FALLING_EDGE,
	/**< Falling edge event. */
};

/**
 * @brief Structure holding event info.
 */
struct gpiod_line_event {
	struct timespec ts;
	/**< Best estimate of time of event occurrence. */
	int event_type;
	/**< Type of the event that occurred. */
	int offset;
	/**< Offset of line on which the event occurred. */
};

/**
 * @brief Wait for an event on a single line.
 * @param line GPIO line object.
 * @param timeout Wait time limit.
 * @return 0 if wait timed out, -1 if an error occurred, 1 if an event
 *         occurred.
 */
int gpiod_line_event_wait(struct gpiod_line *line,
			  const struct timespec *timeout);

/**
 * @brief Wait for events on a set of lines.
 * @param bulk Set of GPIO lines to monitor.
 * @param timeout Wait time limit.
 * @param event_bulk Bulk object in which to store the line handles on which
 *                   events occurred. Can be NULL.
 * @return 0 if wait timed out, -1 if an error occurred, 1 if at least one
 *         event occurred.
 */
int gpiod_line_event_wait_bulk(struct gpiod_line_bulk *bulk,
			       const struct timespec *timeout,
			       struct gpiod_line_bulk *event_bulk);

/**
 * @brief Read next pending event from the GPIO line.
 * @param line GPIO line object.
 * @param event Buffer to which the event data will be copied.
 * @return 0 if the event was read correctly, -1 on error.
 * @note This function will block if no event was queued for this line.
 */
int gpiod_line_event_read(struct gpiod_line *line,
			  struct gpiod_line_event *event);

/**
 * @brief Read up to a certain number of events from the GPIO line.
 * @param line GPIO line object.
 * @param events Buffer to which the event data will be copied. Must hold at
 *               least the amount of events specified in num_events.
 * @param num_events Specifies how many events can be stored in the buffer.
 * @return On success returns the number of events stored in the buffer, on
 *         failure -1 is returned.
 */
int gpiod_line_event_read_multiple(struct gpiod_line *line,
				   struct gpiod_line_event *events,
				   unsigned int num_events);

/**
 * @brief Get the event file descriptor.
 * @param line GPIO line object.
 * @return Number of the event file descriptor or -1 if the user tries to
 *         retrieve the descriptor from a line that wasn't configured for
 *         event monitoring.
 *
 * Users may want to poll the event file descriptor on their own. This routine
 * allows to access it.
 */
int gpiod_line_event_get_fd(struct gpiod_line *line);

/**
 * @brief Read the last GPIO event directly from a file descriptor.
 * @param fd File descriptor.
 * @param event Buffer in which the event data will be stored.
 * @return 0 if the event was read correctly, -1 on error.
 *
 * Users who directly poll the file descriptor for incoming events can also
 * directly read the event data from it using this routine. This function
 * translates the kernel representation of the event to the libgpiod format.
 */
int gpiod_line_event_read_fd(int fd, struct gpiod_line_event *event);

/**
 * @brief Read up to a certain number of events directly from a file descriptor.
 * @param fd File descriptor.
 * @param events Buffer to which the event data will be copied. Must hold at
 *               least the amount of events specified in num_events.
 * @param num_events Specifies how many events can be stored in the buffer.
 * @return On success returns the number of events stored in the buffer, on
 *         failure -1 is returned.
 */
int gpiod_line_event_read_fd_multiple(int fd, struct gpiod_line_event *events,
				      unsigned int num_events);

/**
 * @}
 *
 * @}
 *
 * @defgroup misc Stuff that didn't fit anywhere else
 * @{
 *
 * Various libgpiod-related functions.
 */

/**
 * @brief Get the API version of the library as a human-readable string.
 * @return Human-readable string containing the library version.
 */
const char *gpiod_version_string(void);

/**
 * @}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBGPIOD_GPIOD_H__ */
