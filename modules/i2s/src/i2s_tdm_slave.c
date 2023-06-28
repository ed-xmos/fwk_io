// Copyright 2023 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.
#include <string.h>
#include <stdio.h>
#include <xclib.h>

#include <xcore/port.h>
#include <xcore/clock.h>
#include <xcore/assert.h>

#include "i2s.h"
#include "i2s_tdm_slave.h"

void i2s_tdm_slave_init(
        i2s_tdm_ctx_t *ctx,
        i2s_callback_group_t *i2s_cbg,
        port_t p_dout[],
        size_t num_out,
        port_t p_din[],
        size_t num_in,
        port_t p_fsync,
        port_t p_bclk,
        xclock_t bclk,
        uint32_t tx_offset,
        uint32_t fsync_len,
        uint32_t word_len,
        uint32_t ch_len,
        uint32_t ch_per_frame,
        i2s_slave_bclk_polarity_t slave_bclk_pol,
        void *app_data)
{
    memset(ctx, 0, sizeof(i2s_tdm_ctx_t));
    ctx->i2s_cbg = i2s_cbg;

    xassert(num_out <= I2S_TDM_MAX_POUT_CNT);
    memcpy((void*)ctx->p_dout, p_dout, sizeof(port_t) * num_out);
    xassert(num_in <= I2S_TDM_MAX_PIN_CNT);
    memcpy((void*)ctx->p_din, p_din, sizeof(port_t) * num_in);
    ctx->num_out = num_out;
    ctx->num_in = num_in;

    ctx->p_fsync = p_fsync;
    ctx->p_bclk = p_bclk;
    ctx->bclk = bclk;
    ctx->tx_offset = tx_offset;
    ctx->fsync_len = 1; /* Does not matter for slave as we only care about rising or falling edge */
    ctx->word_len = word_len;
    ctx->ch_len = ch_len;
    ctx->ch_per_frame = ch_per_frame;
    ctx->slave_bclk_polarity = I2S_SLAVE_SAMPLE_ON_BCLK_RISING;
    ctx->app_data = app_data;
}

void i2s_tdm_slave_tx_16_init(
        i2s_tdm_ctx_t *ctx,
        i2s_callback_group_t *i2s_cbg,
        port_t p_dout,
        port_t p_fsync,
        port_t p_bclk,
        xclock_t bclk,
        uint32_t tx_offset,
        i2s_slave_bclk_polarity_t slave_bclk_polarity,
        void *app_data)
{
    port_t pdout[I2S_TDM_MAX_POUT_CNT]; 
    pdout[0] = p_dout;

    i2s_tdm_slave_init(
        ctx,
        i2s_cbg,
        pdout,
        1,
        NULL,
        0,
        p_fsync,
        p_bclk,
        bclk,
        tx_offset,
        1,  /* fsync_len: Does not matter for slave as we only care about edge */
        32, /* word len */
        32, /* ch len */
        16,  /* ch per frame */
        slave_bclk_polarity,
        app_data);
}

static void i2s_tdm_slave_init_resources(
        i2s_tdm_ctx_t *ctx)
{
    port_enable(ctx->p_bclk);
    clock_enable(ctx->bclk);
    clock_set_source_port(ctx->bclk, ctx->p_bclk);
    clock_set_divide(ctx->bclk, 0);

    for(int i=0; i<ctx->num_out; i++) {
        port_start_buffered(ctx->p_dout[i], 32);
        port_set_clock(ctx->p_dout[i], ctx->bclk);
        port_clear_buffer(ctx->p_dout[i]);
    }
    for(int i=0; i<ctx->num_in; i++) {
        port_start_buffered(ctx->p_din[i], 32);
        port_set_clock(ctx->p_din[i], ctx->bclk);
        port_clear_buffer(ctx->p_din[i]);
    }
    port_enable(ctx->p_fsync);
    port_set_clock(ctx->p_fsync, ctx->bclk);
    port_clear_buffer(ctx->p_fsync);
    
    if (ctx->slave_bclk_polarity == I2S_SLAVE_SAMPLE_ON_BCLK_FALLING) {
        port_set_invert(ctx->p_bclk);
    } else {
        port_set_no_invert(ctx->p_bclk);
    }
    clock_start(ctx->bclk);
}

static void i2s_tdm_slave_deinit_resources(
        i2s_tdm_ctx_t *ctx)
{
    clock_disable(ctx->bclk);
    port_disable(ctx->p_bclk);

    for(int i=0; i<ctx->num_out; i++) {
        port_disable(ctx->p_dout[i]);
    }
    for(int i=0; i<ctx->num_in; i++) {
        port_disable(ctx->p_din[i]);
    }
}

void i2s_tdm_slave_tx_16_thread(
        i2s_tdm_ctx_t *ctx)
{
    uint32_t out_samps[I2S_TDM_MAX_CH_PER_FRAME];
    uint32_t fsync_val = 0;

    while(1) {
        if (ctx->i2s_cbg->init != NULL) {
            ctx->i2s_cbg->init((void*)ctx, NULL);
        }
        xassert(ctx->num_out == 1);
        i2s_tdm_slave_init_resources(ctx);

        /* Get first frame data */
        ctx->i2s_cbg->send((void*)ctx, ctx->ch_per_frame, (int32_t*)out_samps);

        uint32_t port_frame_time = (ctx->ch_per_frame * ctx->word_len);

        /* Wait for first fsync rising edge to occur */
        port_set_trigger_in_equal(ctx->p_fsync, 0);
        (void) port_in(ctx->p_fsync);
        port_set_trigger_in_equal(ctx->p_fsync, 1);
        (void) port_in(ctx->p_fsync);
        port_timestamp_t fsync_edge_time = port_get_trigger_time(ctx->p_fsync);
        port_start_buffered(ctx->p_fsync, 32);

        /* Setup trigger times */
        uint32_t fsync_trig_time = port_frame_time + fsync_edge_time - ctx->word_len - 2;
        port_set_trigger_time(ctx->p_fsync, fsync_trig_time);
        port_set_trigger_time(ctx->p_dout[0], port_frame_time + fsync_edge_time + ctx->tx_offset);

        // (void) port_in(ctx->p_fsync);
        for (int i=0; i<ctx->ch_per_frame; i++) {
            port_out(ctx->p_dout[0], bitrev(out_samps[i]));
            // (void) port_in(ctx->p_fsync);
        }

        while(1) {
            /* We only care about seeing the rising edge */
            // fsync_val &= 0x8000000;
            // if (fsync_val != 0x8000000) {
            //     printf("fsync error, expected 0x%x, was 0x%x\n", 0x00000001, (unsigned int)fsync_val);
            //     break;
            // }

            /* Get frame data and tx */
            ctx->i2s_cbg->send((void*)ctx, ctx->ch_per_frame, (int32_t*)out_samps);

            for (int i=0; i<ctx->ch_per_frame; i++) {
                port_out(ctx->p_dout[0], bitrev(out_samps[i]));
                // fsync_val = port_in(ctx->p_fsync);

                // if (i == 1) {
                //     fsync_val &= 0x00000003;
                //     if (fsync_val != 0x00000002) {
                //         printf("fsync error, expected 0x%x, was 0x%x\n", 0x00000001, (unsigned int)fsync_val);
                //         break;
                //     }
                // }


                // printf("fsync_val[%d] 0x%x\n", i, fsync_val);
            }

            /* Check for exit condition */
            if (ctx->i2s_cbg->restart_check != NULL) {
                i2s_restart_t restart = ctx->i2s_cbg->restart_check((void*)ctx);

                if (restart == I2S_RESTART) {
                    break;
                } else if (restart == I2S_SHUTDOWN) {
                    i2s_tdm_slave_deinit_resources(ctx);
                    return;
                }
            }
        }
        i2s_tdm_slave_deinit_resources(ctx);
    }
}

void i2s_slave_tdm_thread(
        i2s_tdm_ctx_t *ctx)
{
    printf("Not implemented\n");
    xassert(0); /* Not yet implemented */
    while(1) {
        ;
    }
}
