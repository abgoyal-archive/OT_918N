
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <asm/mach/time.h>

#include <plat/mtu.h>

void __iomem *mtu_base; /* ssigned by machine code */

static cycle_t nmdk_read_timer_dummy(struct clocksource *cs)
{
	return 0;
}

/* clocksource: MTU decrements, so we negate the value being read. */
static cycle_t nmdk_read_timer(struct clocksource *cs)
{
	return -readl(mtu_base + MTU_VAL(0));
}

static struct clocksource nmdk_clksrc = {
	.name		= "mtu_0",
	.rating		= 200,
	.read		= nmdk_read_timer_dummy,
	.mask		= CLOCKSOURCE_MASK(32),
	.shift		= 20,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

unsigned long long notrace sched_clock(void)
{
	return clocksource_cyc2ns(nmdk_clksrc.read(
				  &nmdk_clksrc),
				  nmdk_clksrc.mult,
				  nmdk_clksrc.shift);
}

/* Clockevent device: use one-shot mode */
static void nmdk_clkevt_mode(enum clock_event_mode mode,
			     struct clock_event_device *dev)
{
	u32 cr;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_err("%s: periodic mode not supported\n", __func__);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Load highest value, enable device, enable interrupts */
		cr = readl(mtu_base + MTU_CR(1));
		writel(0, mtu_base + MTU_LR(1));
		writel(cr | MTU_CRn_ENA, mtu_base + MTU_CR(1));
		writel(0x2, mtu_base + MTU_IMSC);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		/* disable irq */
		writel(0, mtu_base + MTU_IMSC);
		break;
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int nmdk_clkevt_next(unsigned long evt, struct clock_event_device *ev)
{
	/* writing the value has immediate effect */
	writel(evt, mtu_base + MTU_LR(1));
	return 0;
}

static struct clock_event_device nmdk_clkevt = {
	.name		= "mtu_1",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.rating		= 200,
	.set_mode	= nmdk_clkevt_mode,
	.set_next_event	= nmdk_clkevt_next,
};

static irqreturn_t nmdk_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evdev = dev_id;

	writel(1 << 1, mtu_base + MTU_ICR); /* Interrupt clear reg */
	evdev->event_handler(evdev);
	return IRQ_HANDLED;
}

static struct irqaction nmdk_timer_irq = {
	.name		= "Nomadik Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= nmdk_timer_interrupt,
	.dev_id		= &nmdk_clkevt,
};

void __init nmdk_timer_init(void)
{
	unsigned long rate;
	struct clk *clk0;
	struct clk *clk1;
	u32 cr;

	clk0 = clk_get_sys("mtu0", NULL);
	BUG_ON(IS_ERR(clk0));

	clk1 = clk_get_sys("mtu1", NULL);
	BUG_ON(IS_ERR(clk1));

	clk_enable(clk0);
	clk_enable(clk1);

	/*
	 * Tick rate is 2.4MHz for Nomadik and 110MHz for ux500:
	 * use a divide-by-16 counter if it's more than 16MHz
	 */
	cr = MTU_CRn_32BITS;;
	rate = clk_get_rate(clk0);
	if (rate > 16 << 20) {
		rate /= 16;
		cr |= MTU_CRn_PRESCALE_16;
	} else {
		cr |= MTU_CRn_PRESCALE_1;
	}

	/* Timer 0 is the free running clocksource */
	writel(cr, mtu_base + MTU_CR(0));
	writel(0, mtu_base + MTU_LR(0));
	writel(0, mtu_base + MTU_BGLR(0));
	writel(cr | MTU_CRn_ENA, mtu_base + MTU_CR(0));

	nmdk_clksrc.mult = clocksource_hz2mult(rate, nmdk_clksrc.shift);
	/* Now the scheduling clock is ready */
	nmdk_clksrc.read = nmdk_read_timer;

	if (clocksource_register(&nmdk_clksrc))
		pr_err("timer: failed to initialize clock source %s\n",
		       nmdk_clksrc.name);

	/* Timer 1 is used for events, fix according to rate */
	cr = MTU_CRn_32BITS;
	rate = clk_get_rate(clk1);
	if (rate > 16 << 20) {
		rate /= 16;
		cr |= MTU_CRn_PRESCALE_16;
	} else {
		cr |= MTU_CRn_PRESCALE_1;
	}
	writel(cr | MTU_CRn_ONESHOT, mtu_base + MTU_CR(1)); /* off, currently */
	nmdk_clkevt.mult = div_sc(rate, NSEC_PER_SEC, nmdk_clkevt.shift);
	nmdk_clkevt.max_delta_ns =
		clockevent_delta2ns(0xffffffff, &nmdk_clkevt);
	nmdk_clkevt.min_delta_ns =
		clockevent_delta2ns(0x00000002, &nmdk_clkevt);
	nmdk_clkevt.cpumask	= cpumask_of(0);

	/* Register irq and clockevents */
	setup_irq(IRQ_MTU0, &nmdk_timer_irq);
	clockevents_register_device(&nmdk_clkevt);
}
